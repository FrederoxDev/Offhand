#include "features/OffhandRendering.hpp"

#include "features/OffhandSwing.hpp"
#include "offhand/HandSwapScope.hpp"
#include "offhand/Hands.hpp"

#include <amethyst/runtime/ModContext.hpp>
#include <amethyst/runtime/utility/InlineHook.hpp>
#include <client/game/IClientInstance.hpp>
#include <client/gui/screens/ScreenContext.hpp>
#include <client/model/models/DataDrivenModel.hpp>
#include <client/options/IOptionRegistry.hpp>
#include <client/player/LocalPlayer.hpp>
#include <client/renderer/BaseActorRenderContext.hpp>
#include <client/renderer/block/BlockGraphics.hpp>
#include <client/renderer/block/BlockTessellator.hpp>
#include <client/renderer/block/TessellationPipeline/BlockSchematic.hpp>
#include <client/renderer/block/TessellationPipeline/ItemTransforms.hpp>
#include <client/renderer/game/ItemInHandRenderer.hpp>
#include <client/world/level/block/ClientBlockData.hpp>
#include <entity/components/ItemInUseComponent.hpp>
#include <fallback/globals/VanillaItemNames.hpp>
#include <MinecraftRenderer/game/ItemRenderCall.hpp>
#include <Platform/brstd/function_ref.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/MatrixStack.hpp>
#include <SharedTypes/legacy/item/UseAnimation.hpp>
#include <world/actor/RenderParams.hpp>
#include <world/actor/animation/AttachableSlotIndex.hpp>
#include <world/actor/player/Player.hpp>
#include <world/item/Item.hpp>
#include <world/item/ItemStack.hpp>
#include <world/item/components/ComponentItem.hpp>
#include <world/level/block/Block.hpp>
#include <world/level/block/BlockType.hpp>
#include <world/level/block/IClientBlockData.hpp>
#include <world/level/block/components/BlockComponentDirectData.hpp>
#include <world/level/block/components/BlockGeometryComponent.hpp>
#include <world/level/block/components/BlockItemVisualComponent.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

using SharedTypes::Legacy::UseAnimation;
using ItemTransformType = ClientBlockPipeline::ItemTransforms::Type;

constexpr float PI = std::numbers::pi_v<float>;

// BrushItem::SWING_DURATION.
constexpr int BRUSH_SWING_DURATION = 20;

// renderOffhandItem only receives part of renderFirstPerson's item flags.
thread_local ItemContextFlags firstPersonItemFlags = ItemContextFlags::None;

// Set by renderFirstPerson while it hides the offhand use from the main hand.
thread_local bool hidingOffhandUse = false;
thread_local int offhandUseDuration = 0;

// Set by renderFirstPerson while it hides the offhand swing from the main hand.
thread_local bool hidingOffhandSwing = false;
thread_local float offhandAttackAnim = 0.0f;
thread_local float offhandOAttackAnim = 0.0f;

// Makes _applyDefaultItemTransforms mirror the offhand item's transforms along with the rest of its pose.
thread_local bool mirroringItemTransforms = false;

// Non-null only inside renderFirstPerson, so the attachable hooks can tell a first person draw apart.
thread_local Player* firstPersonPlayer = nullptr;

struct MatrixStackRefAccess : MatrixStack::MatrixStackRef {
    using MatrixStack::MatrixStackRef::mat;
    using MatrixStack::MatrixStackRef::stack;
};

// Java's left arm pose mirrors the right one: x offsets and Y and Z rotations are negated
// (ItemInHandRenderer#renderArmWithItem, ItemTransform#apply). As a whole transform that is S * transform * S with
// S = scale(-1, 1, 1), which keeps the winding.
Matrix mirrored(const Matrix& transform)
{
    Matrix result = transform;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if ((column == 0) != (row == 0))
                result._m[column][row] = -result._m[column][row];
        }
    }
    return result;
}

int itemUseDuration(const Player& player)
{
    if (hidingOffhandUse)
        return offhandUseDuration;

    const ItemInUseComponent* component = player.getEntity().tryGetComponent<ItemInUseComponent>();
    return component != nullptr ? component->mDuration : 0;
}

float offhandSwingProgress(const Player& player, float frameAlpha)
{
    if (hidingOffhandSwing)
        return OffhandSwing::interpolateAttackAnim(offhandAttackAnim, offhandOAttackAnim, frameAlpha);

    return OffhandSwing::getAttackAnim(player, frameAlpha);
}

bool usesNewTessellation(const Block& block)
{
    const auto& fallback = BlockGraphics::mVanillaBlocksWithFallbackToOldTessellation;
    if (fallback.find(block.getBlockType().getDefaultState().mNetworkId) != fallback.end())
        return false;

    return block.mDirectData.mUseNewTessellation;
}

bool setsMatrixFromJson(const ItemStack& item)
{
    const Item* type = item.getItem();
    return type != nullptr && type->isComponentBased() && static_cast<const ComponentItem*>(type)->shouldUseJsonForRenderMatrix();
}

// ClientBlockLogic::getItemDisplayTransform is inlined.
Matrix getItemDisplayTransform(const Block& block, ItemTransformType type)
{
    Matrix result;
    if (block.mClientData == nullptr)
        return result;

    const auto visitor = [&](const ClientBlockData& data) {
        const BlockItemVisualComponent* itemVisual = data.mDirectData.mBlockItemVisualComponent;
        const BlockGeometryComponent* geometry = itemVisual != nullptr
            ? itemVisual->mGeometryComponent.get()
            : block.mDirectData.mBlockGeometryComponent;

        if (geometry != nullptr && geometry->mBlockSchematic != nullptr) {
            const auto& transforms = geometry->mBlockSchematic->mItemTransforms;
            const auto match = std::find_if(transforms.begin(), transforms.end(), [type](const auto& transform) { return transform.mType == type; });
            if (match != transforms.end()) {
                result = match->mTransform;
                return;
            }
        }

        result = type <= ItemTransformType::SHELF ? ClientBlockPipeline::ItemTransforms::getDefaultTransformMatrix(type) : Matrix();
    };

    block.mClientData->visit(visitor);
    return result;
}

// ItemInHandRenderer::_shouldRenderOffhandItem is inlined.
bool shouldRenderOffhandItem(const ItemInHandRenderer& renderer, const Player& player)
{
    const ItemStack& mainhand = renderer.mItem;
    const ItemStack& offhand = renderer.mOffHandItem;

    if (mainhand.isInstance(VanillaItemNames::FilledMap, false) || mainhand.isInstance(VanillaItemNames::PhotoItem, false)) {
        return offhand.isInstance(VanillaItemNames::FilledMap, false)
            || offhand.isInstance(VanillaItemNames::PhotoItem, false)
            || offhand.isInstance(VanillaItemNames::Shield, false);
    }

    const ItemInUseComponent* component = player.getEntity().tryGetComponent<ItemInUseComponent>();
    const bool mainhandInUse = component != nullptr && component->mDuration > 0;

    if (mainhand.isInstance(VanillaItemNames::Bow, false))
        return !mainhandInUse;

    if (mainhand.isInstance(VanillaItemNames::Crossbow, false))
        return !mainhandInUse && !mainhand.hasChargedItem();

    return true;
}

// ItemInHandRenderer::_applyUseAnimation is inlined. Its split screen eating height correction is left out, as it needs
// the renderer's output size.
void applyUseAnimation(Matrix& pose, const ItemStack& item, int useDuration, float frameAlpha)
{
    const Item* type = item.getItem();
    const UseAnimation animation = type->mUseAnim;

    switch (animation) {
    case UseAnimation::Eat:
    case UseAnimation::Drink:
    case UseAnimation::GlowStick:
    case UseAnimation::Sparkler: {
        const float maxDuration = static_cast<float>(type->getMaxUseDuration(&item));
        const float t = static_cast<float>(useDuration) - frameAlpha + 1.0f;
        const float progress = 1.0f - t / maxDuration;

        float settle = 1.0f - progress;
        settle = settle * settle * settle;
        settle = settle * settle * settle;
        settle = settle * settle * settle;
        const float raise = 1.0f - settle;

        const float magnitude = std::abs(std::cos(t / 4.0f * PI) * 0.1f) * (progress > 0.2f ? 1.0f : 0.0f);
        if (animation == UseAnimation::GlowStick || animation == UseAnimation::Sparkler)
            pose.translate(magnitude, 0.0f, 0.0f);
        else
            pose.translate(0.0f, magnitude, 0.0f);

        pose.translate(raise * 0.55f, -raise * 0.5f, 0.0f);
        pose.rotate(raise * 90.0f, 0.0f, 1.0f, 0.0f);
        pose.rotate(raise * 10.0f, 1.0f, 0.0f, 0.0f);
        pose.rotate(raise * 30.0f, 0.0f, 0.0f, 1.0f);
        break;
    }
    case UseAnimation::Camera: {
        const float timeHeld = (static_cast<float>(type->getMaxUseDuration(&item)) - (static_cast<float>(useDuration) - frameAlpha + 1.0f)) / 20.0f;
        const float power = std::min(1.0f, (timeHeld * timeHeld + timeHeld * 2.0f) / 3.0f);

        float settle = 1.0f - power;
        settle = settle * settle * settle;
        settle = settle * settle * settle;
        settle = settle * settle * settle;
        const float raise = 1.0f - settle;

        pose.rotate(raise * 45.0f, 0.0f, 1.0f, 0.0f);
        pose.translate(-1.5f, 0.0f, 0.0f);
        break;
    }
    case UseAnimation::GoatHorn:
        pose.rotate(25.0f, 0.0f, 1.0f, 0.0f);
        pose.rotate(32.742f, 1.0f, 0.0f, 0.0f);
        pose.rotate(1.2f, 0.0f, 0.0f, 1.0f);
        pose.translate(-0.405f, 0.023f, 0.344f);
        break;
    case UseAnimation::Brush: {
        const float sinceSwingStart = static_cast<float>(useDuration % BRUSH_SWING_DURATION) - frameAlpha + 1.0f;
        const float swipe = 1.0f - sinceSwingStart / static_cast<float>(BRUSH_SWING_DURATION);

        pose.translate(0.28f, 0.196f, -0.184f);
        pose.rotate(-80.0f, 1.0f, 0.0f, 0.0f);
        pose.rotate(90.0f, 0.0f, 1.0f, 0.0f);
        pose.rotate(-15.0f + 75.0f * std::cos(swipe * 4.0f * PI), 1.0f, 0.0f, 0.0f);
        pose.translate(0.0f, 0.452f, 0.608f);
        break;
    }
    default:
        break;
    }
}

// ItemInHandRenderer::_applyMainhandItemTransforms is inlined into renderFirstPerson. This is that main hand pose, driven
// by the offhand's swing, use and swap height.
Matrix computeHeldItemPose(const ItemInHandRenderer& renderer, IClientInstance& client, const Player& player, const ItemStack& item, const Block* block, bool useBlockTransforms, float frameAlpha)
{
    const int useDuration = Offhand::isUsingOffhandItem(player) ? itemUseDuration(player) : 0;
    const bool isUsing = useDuration > 0;
    const bool noSwingAnimation = isUsing && item.getItem()->mUseAnim != UseAnimation::None;

    const float swing = noSwingAnimation ? 0.0f : offhandSwingProgress(player, frameAlpha);
    const float sqrtSwing = std::sqrt(swing);
    const float swing3 = std::sin(swing * swing * PI);
    const float swing1 = std::sin(swing * PI);
    const float swing2 = std::sin(sqrtSwing * PI);

    Matrix pose;
    if (isUsing)
        applyUseAnimation(pose, item, useDuration, frameAlpha);
    else
        pose.translate(-swing2 * 0.4f, std::sin(sqrtSwing * PI * 2.0f) * 0.2f, -swing1 * 0.2f);

    pose.translate(0.56f, -0.52f, -0.72f);

    const float height = renderer.mOldHeightOffHand + (renderer.mHeightOffHand - renderer.mOldHeightOffHand) * frameAlpha;
    pose.translate(0.0f, -(1.0f - height) * 0.6f, 0.0f);

    pose.rotate(45.0f, 0.0f, 1.0f, 0.0f);
    pose.rotate(-swing3 * 20.0f, 0.0f, 1.0f, 0.0f);
    pose.rotate(-swing2 * 20.0f, 0.0f, 0.0f, 1.0f);
    pose.rotate(-swing2 * 80.0f, 1.0f, 0.0f, 0.0f);

    if (!useBlockTransforms)
        pose.scale(0.4f, 0.4f, 0.4f);

    if (client.getOptions().getBobView()) {
        const float bobHeight = useBlockTransforms ? 0.0044f : 0.011f;
        const float bob = bobHeight * std::sin(static_cast<float>(renderer.mTimer.count() * 2.0));
        pose.translate(0.0f, bob, 0.0f);
        pose.rotate(bob * 360.0f * 0.075f, 1.0f, 0.0f, 0.0f);
    }

    if (useBlockTransforms && block != nullptr) {
        pose.mult(ClientBlockPipeline::ItemTransforms::getFixSpaceTransformMatrix(ItemTransformType::FIRSTPERSON_RIGHTHAND));
        pose.mult(getItemDisplayTransform(*block, ItemTransformType::FIRSTPERSON_RIGHTHAND));
    }

    if (item.getItem()->mIsMirroredArt && !useBlockTransforms)
        pose.rotate(180.0f, 0.0f, 1.0f, 0.0f);

    return pose;
}

Amethyst::InlineHook<decltype(&ItemInHandRenderer::renderOffhandItem)> _ItemInHandRenderer_renderOffhandItem;

// Java renders the offhand like the main hand, mirrored onto the left arm.
void ItemInHandRenderer_renderOffhandItem(ItemInHandRenderer* self, BaseActorRenderContext& renderContext, Player& player, ItemContextFlags itemFlags)
{
    const ItemStack item = self->mOffHandItem;
    if (!Offhand::hasItem(item) || item.isInstance(VanillaItemNames::FilledMap, false) || item.isInstance(VanillaItemNames::PhotoItem, false) || setsMatrixFromJson(item)) {
        _ItemInHandRenderer_renderOffhandItem(self, renderContext, player, itemFlags);
        return;
    }

    if (!shouldRenderOffhandItem(*self, player))
        return;

    const Block* block = item.getBlockForRendering();
    const bool useBlockTransforms = block != nullptr && usesNewTessellation(*block)
        && BlockTessellator::canRender(BlockGraphics::getForBlock(*block)->mBlockShape);

    const float frameAlpha = renderContext.getFrameAlpha(player);
    const Matrix pose = computeHeldItemPose(*self, renderContext.mClientInstance, player, item, block, useBlockTransforms, frameAlpha);

    MatrixStack& worldMatrix = renderContext.mScreenContext.camera.worldMatrixStack;
    worldMatrix._push().mult(mirrored(pose));

    // RangedWeaponItem::getAnimationFrameFor and CrossbowItem::getAnimationFrameFor pick the pulling frame from the
    // holder's use duration, which renderFirstPerson hides from the main hand.
    ItemInUseComponent* component = hidingOffhandUse ? player.getEntity().tryGetComponent<ItemInUseComponent>() : nullptr;
    if (component != nullptr)
        component->mDuration = offhandUseDuration;

    mirroringItemTransforms = true;
    self->renderItem(renderContext, player, item, false, firstPersonItemFlags, useBlockTransforms, true);
    mirroringItemTransforms = false;

    if (component != nullptr)
        component->mDuration = 0;

    worldMatrix.pop();
}

Amethyst::InlineHook<decltype(&ItemInHandRenderer::_applyDefaultItemTransforms)> _ItemInHandRenderer__applyDefaultItemTransforms;

void ItemInHandRenderer__applyDefaultItemTransforms(ItemInHandRenderer* self, MatrixStack::MatrixStackRef& worldMatrix, const ItemStack& item, bool isInHandItem, const BlockType* blockType, BlockShape blockShape, const ItemRenderCall* renderObjectCall, float heldItemScale, bool posAndRotSetByJSON)
{
    if (!mirroringItemTransforms) {
        _ItemInHandRenderer__applyDefaultItemTransforms(self, worldMatrix, item, isInHandItem, blockType, blockShape, renderObjectCall, heldItemScale, posAndRotSetByJSON);
        return;
    }

    Matrix& top = *(worldMatrix.*(&MatrixStackRefAccess::mat));
    const Matrix parent = top;
    top = Matrix();

    _ItemInHandRenderer__applyDefaultItemTransforms(self, worldMatrix, item, isInHandItem, blockType, blockShape, renderObjectCall, heldItemScale, posAndRotSetByJSON);

    const Matrix transforms = top;
    top = parent;
    top.mult(mirrored(transforms));

    // Mirroring the pose also mirrors a flat item's sprite within its plane, showing it from behind with its diagonal
    // the wrong way round. Java's left hand shows the main hand's picture mirrored, so the sprite is mirrored back and
    // its depth (TextureTessellator's pixelDepth, along y) flipped.
    if (renderObjectCall != nullptr && renderObjectCall->mIsFlat)
        top.scale(-1.0f, -1.0f, 1.0f);

    (worldMatrix.*(&MatrixStackRefAccess::stack))->_isDirty = true;
    self->mTransform = top;
}

// minecraft:bow's attachable has no binding, so DataDrivenModel::setupAttachableNoChecks parents it to the holder's
// rightitem bone whichever hand holds it. The crossbow, shield and trident bind to their slot's bone.
bool attachesToRightItem(const ItemStack& item)
{
    return item.isInstance(VanillaItemNames::Bow, false);
}

// renderFirstPerson never calls renderOffhandItem for an item with an attachable, so the offhand bow is otherwise drawn
// only by its attachable, on the right arm.
bool mirrorsOffhandAttachable(const ItemStack& item, AttachableSlotIndex slot, const Actor& actor)
{
    return slot == AttachableSlotIndex::OffhandItem && firstPersonPlayer != nullptr
        && &actor == static_cast<const Actor*>(firstPersonPlayer) && attachesToRightItem(item);
}

// The bow's attachable reads only the main hand: query.main_hand_item_use_duration gates its pull animation and
// query.get_animation_frame takes the pulling frame from the selected item.
class OffhandAttachableScope {
public:
    explicit OffhandAttachableScope(Player& player)
        : mComponent(player.getEntity().tryGetComponent<ItemInUseComponent>())
        , mDuration(mComponent != nullptr ? mComponent->mDuration : 0)
        , mHands(player)
    {
        if (mComponent != nullptr)
            mComponent->mDuration = hidingOffhandUse ? offhandUseDuration : 0;
    }

    ~OffhandAttachableScope()
    {
        if (mComponent != nullptr)
            mComponent->mDuration = mDuration;
    }

    OffhandAttachableScope(const OffhandAttachableScope&) = delete;
    OffhandAttachableScope& operator=(const OffhandAttachableScope&) = delete;

private:
    ItemInUseComponent* mComponent;
    int mDuration;
    Offhand::HandSwapScope mHands;
};

Amethyst::InlineHook<decltype(&DataDrivenModel::setupAttachable)> _DataDrivenModel_setupAttachable;

void DataDrivenModel_setupAttachable(DataDrivenModel* self, const ItemStack& itemInstance, const AttachableSlotIndex& attachableSlotIndex, RenderParams& renderParams, Actor& actor)
{
    if (!mirrorsOffhandAttachable(itemInstance, attachableSlotIndex, actor)) {
        _DataDrivenModel_setupAttachable(self, itemInstance, attachableSlotIndex, renderParams, actor);
        return;
    }

    // itemInstance aliases the offhand slot, which the swap overwrites.
    const ItemStack item = itemInstance;
    OffhandAttachableScope scope(*firstPersonPlayer);
    _DataDrivenModel_setupAttachable(self, item, attachableSlotIndex, renderParams, actor);
}

Amethyst::InlineHook<decltype(&DataDrivenModel::renderAttachable)> _DataDrivenModel_renderAttachable;

void DataDrivenModel_renderAttachable(DataDrivenModel* self, const ItemStack& itemInstance, const AttachableSlotIndex& attachableSlotIndex, RenderParams& renderParams, Actor& actor)
{
    if (!mirrorsOffhandAttachable(itemInstance, attachableSlotIndex, actor)) {
        _DataDrivenModel_renderAttachable(self, itemInstance, attachableSlotIndex, renderParams, actor);
        return;
    }

    const ItemStack item = itemInstance;

    // Bound to the right arm's rightitem bone, so mirroring across x puts the main hand's bow on the left, in the same
    // space renderOffhandItem mirrors other items in.
    MatrixStack& worldMatrix = renderParams.mBaseActorRenderContext->mScreenContext.camera.worldMatrixStack;
    worldMatrix._push().scale(-1.0f, 1.0f, 1.0f);

    {
        OffhandAttachableScope scope(*firstPersonPlayer);
        _DataDrivenModel_renderAttachable(self, item, attachableSlotIndex, renderParams, actor);
    }

    worldMatrix.pop();
}

Amethyst::InlineHook<decltype(&ItemInHandRenderer::renderFirstPerson)> _ItemInHandRenderer_renderFirstPerson;

// ItemInHandRenderer animates the main hand for any item use or swing. Java animates only the hand doing it
// (LivingEntity#getUsedItemHand, LivingEntity#swingingArm), so an offhand use or swing is hidden from the main hand.
void ItemInHandRenderer_renderFirstPerson(ItemInHandRenderer* self, BaseActorRenderContext& renderContext, const Matrix& prevProj, ItemContextFlags itemFlags)
{
    firstPersonItemFlags = itemFlags;

    LocalPlayer* player = self->mClient.getLocalPlayer();
    if (player == nullptr) {
        _ItemInHandRenderer_renderFirstPerson(self, renderContext, prevProj, itemFlags);
        return;
    }

    ItemInUseComponent* component = Offhand::isUsingOffhandItem(*player)
        ? player->getEntity().tryGetComponent<ItemInUseComponent>()
        : nullptr;
    if (component != nullptr) {
        offhandUseDuration = component->mDuration;
        hidingOffhandUse = true;
        component->mDuration = 0;
    }

    const bool offhandSwing = OffhandSwing::isOffhandSwing(*player);
    if (offhandSwing) {
        offhandAttackAnim = player->mAttackAnim;
        offhandOAttackAnim = player->mOAttackAnim;
        hidingOffhandSwing = true;
        player->mAttackAnim = 0.0f;
        player->mOAttackAnim = 0.0f;
    }

    firstPersonPlayer = player;
    _ItemInHandRenderer_renderFirstPerson(self, renderContext, prevProj, itemFlags);
    firstPersonPlayer = nullptr;

    if (offhandSwing) {
        player->mAttackAnim = offhandAttackAnim;
        player->mOAttackAnim = offhandOAttackAnim;
        hidingOffhandSwing = false;
    }

    if (component != nullptr) {
        component->mDuration = offhandUseDuration;
        hidingOffhandUse = false;
    }
}

} // namespace

void OffhandRendering::Initialize()
{
    auto& hooks = Amethyst::GetHookManager();
    HOOK(ItemInHandRenderer, renderOffhandItem);
    HOOK(ItemInHandRenderer, _applyDefaultItemTransforms);
    HOOK(ItemInHandRenderer, renderFirstPerson);
    HOOK(DataDrivenModel, setupAttachable);
    HOOK(DataDrivenModel, renderAttachable);
}
