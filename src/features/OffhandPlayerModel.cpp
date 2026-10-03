#include "features/OffhandPlayerModel.hpp"

#include "features/OffhandSwing.hpp"
#include "offhand/Hands.hpp"

#include <amethyst/runtime/ModContext.hpp>
#include <amethyst/runtime/utility/InlineHook.hpp>
#include <Core/String/StringHash.hpp>
#include <SharedTypes/legacy/item/UseAnimation.hpp>
#include <util/MolangScriptExpressionArg.hpp>
#include <util/MolangVariableMap.hpp>
#include <world/actor/LegacyMolangVariableUpdate.hpp>
#include <world/actor/player/Player.hpp>
#include <world/actor/player/PlayerItemInUse.hpp>
#include <world/item/Item.hpp>
#include <world/item/ItemStack.hpp>
#include <world/actor/player/PlayerInventory.hpp>
#include <entity/components/ItemInUseComponent.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace {

using SharedTypes::Legacy::UseAnimation;

struct MolangVariableName {
    uint64_t hash;
    const char* name;
};

constexpr MolangVariableName variable(const char* name)
{
    return {HashedString::computeHash(name), name};
}

// Written by LegacyMolangVariableUpdate::updateEntitySpecificMolangVariables.
constexpr MolangVariableName ATTACK_TIME = variable("variable.attack_time");
constexpr MolangVariableName LEFT_ARM_SWIM_AMOUNT = variable("variable.left_arm_swim_amount");
constexpr MolangVariableName RIGHT_ARM_SWIM_AMOUNT = variable("variable.right_arm_swim_amount");
constexpr MolangVariableName USE_ITEM_INTERVAL_PROGRESS = variable("variable.use_item_interval_progress");
constexpr MolangVariableName USE_ITEM_STARTUP_PROGRESS = variable("variable.use_item_startup_progress");
constexpr MolangVariableName CHARGE_AMOUNT = variable("variable.charge_amount");
constexpr MolangVariableName IS_BRANDISHING_SPEAR = variable("variable.is_brandishing_spear");
constexpr MolangVariableName IS_HOLDING_SPYGLASS = variable("variable.is_holding_spyglass");
constexpr MolangVariableName IS_TOOTING_GOAT_HORN = variable("variable.is_tooting_goat_horn");
constexpr MolangVariableName IS_USING_BRUSH = variable("variable.is_using_brush");

// Read by the resource pack's animation.player.attack.rotations and animation.humanoid.use_item_progress.
constexpr MolangVariableName OFFHAND_ATTACK_TIME = variable("variable.offhand_attack_time");
constexpr MolangVariableName OFFHAND_USE_ITEM_INTERVAL_PROGRESS = variable("variable.offhand_use_item_interval_progress");
constexpr MolangVariableName OFFHAND_USE_ITEM_STARTUP_PROGRESS = variable("variable.offhand_use_item_startup_progress");

// Read by the resource pack's offhand_poses.animation.json and player controllers. variable.offhand_pose mirrors the
// held item pose onto the left arm; the rest stand in for main hand only queries.
constexpr MolangVariableName OFFHAND_POSE = variable("variable.offhand_pose");
constexpr MolangVariableName OFFHAND_ITEM_USE = variable("variable.offhand_item_use");
constexpr MolangVariableName OFFHAND_ITEM_USE_NORMALIZED = variable("variable.offhand_item_use_normalized");
constexpr MolangVariableName OFFHAND_CROSSBOW_CHARGED = variable("variable.offhand_crossbow_charged");

// TridentItem.THROW_THRESHOLD_TIME, which LegacyMolangVariableUpdate charges variable.charge_amount over.
constexpr float TRIDENT_THROW_THRESHOLD_TIME = 10.0f;

// controller.animation.player.root only plays use_item_progress while variable.use_item_interval_progress is above
// zero. This keeps it playing for the offhand without visibly turning the right arm.
constexpr float USE_ITEM_PROGRESS_PLAYING = 0.0001f;

float getFloat(const MolangVariableMap& variables, const MolangVariableName& variable)
{
    const MolangScriptArg& value = variables.getMolangVariable(variable.hash, variable.name);
    return value.mType == MolangScriptArgType::Float ? value.mPOD.mFloat : 0.0f;
}

// MolangScriptArg(float) is inlined. A zeroed Float argument owns nothing, so it is never destroyed.
void setFloat(MolangVariableMap& variables, const MolangVariableName& variable, float value)
{
    alignas(MolangScriptArg) std::array<std::byte, sizeof(MolangScriptArg)> storage{};
    MolangScriptArg* argument = reinterpret_cast<MolangScriptArg*>(storage.data());
    argument->mType = MolangScriptArgType::Float;
    argument->mPOD.mFloat = value;
    variables.setMolangVariable(variable.hash, variable.name, *argument);
}

// HumanoidModel#setupAttackAnimation swings LivingEntity#swingingArm, and the body turns the other way for the left.
void updateSwing(const Player& player, MolangVariableMap& variables)
{
    if (!OffhandSwing::isOffhandSwing(player)) {
        setFloat(variables, OFFHAND_ATTACK_TIME, 0.0f);
        return;
    }

    const float attackTime = getFloat(variables, ATTACK_TIME);
    const float swimAmount = getFloat(variables, LEFT_ARM_SWIM_AMOUNT);
    setFloat(variables, ATTACK_TIME, 0.0f);
    setFloat(variables, OFFHAND_ATTACK_TIME, attackTime);
    setFloat(variables, RIGHT_ARM_SWIM_AMOUNT, swimAmount);
    setFloat(variables, LEFT_ARM_SWIM_AMOUNT, attackTime > 0.0f ? 0.0f : swimAmount);
}

bool isChargedCrossbow(const ItemStack& item)
{
    return Offhand::hasItem(item) && item.getItem()->mUseAnim == UseAnimation::Crossbow && item.hasChargedItem();
}

bool isEatingAnimation(UseAnimation animation)
{
    return animation == UseAnimation::Eat || animation == UseAnimation::Drink || animation == UseAnimation::GlowStick || animation == UseAnimation::Sparkler;
}

// Must match the uses offhand_poses.animation.json has an offhand pose for.
bool hasHeldPose(UseAnimation animation)
{
    return animation == UseAnimation::Bow || animation == UseAnimation::Crossbow || animation == UseAnimation::Spear
        || animation == UseAnimation::Spyglass || animation == UseAnimation::GoatHorn || animation == UseAnimation::Brush;
}

// Java poses item use on the hand it is used from (PlayerRenderer#getArmPose per hand), and an offhand charged crossbow
// takes its hold pose there too.
void updateItemUse(Player& player, MolangVariableMap& variables)
{
    const bool usingItem = Offhand::isUsingItem(player);
    const bool usingOffhandItem = Offhand::isUsingOffhandItem(player);
    const ItemStack& itemInUseStack = player.mItemInUse.mItem;
    const Item* itemInUse = usingItem ? itemInUseStack.getItem() : nullptr;
    const UseAnimation animation = itemInUse != nullptr ? itemInUse->mUseAnim : UseAnimation::None;

    // HumanoidModel#setupAnim poses the hand using its item last, so its pose wins. Otherwise a charged crossbow's hold
    // pose wins, the main hand's first.
    const bool mainhandPosed = !usingOffhandItem && hasHeldPose(animation);
    const bool offhandCrossbowCharged = isChargedCrossbow(Offhand::getItem(player));
    const bool offhandPose = usingOffhandItem
        ? hasHeldPose(animation)
        : offhandCrossbowCharged && !mainhandPosed && !isChargedCrossbow(player.getSupplies().getSelectedItem());

    const ItemInUseComponent* component = player.getEntity().tryGetComponent<ItemInUseComponent>();
    const float remaining = component != nullptr ? static_cast<float>(component->mDuration) : 0.0f;
    const float maxDuration = itemInUse != nullptr ? static_cast<float>(itemInUse->getMaxUseDuration(&itemInUseStack)) : 0.0f;

    setFloat(variables, OFFHAND_POSE, offhandPose ? 1.0f : 0.0f);
    setFloat(variables, OFFHAND_ITEM_USE, usingOffhandItem ? 1.0f : 0.0f);
    setFloat(variables, OFFHAND_ITEM_USE_NORMALIZED, usingOffhandItem && maxDuration > 0.0f ? remaining / maxDuration : 0.0f);
    setFloat(variables, OFFHAND_CROSSBOW_CHARGED, offhandPose && !usingOffhandItem ? 1.0f : 0.0f);

    // A shield in use is shown by the blocking poses alone. Player::getItemUseStartupProgress would otherwise move the
    // arm as if eating.
    if (animation == UseAnimation::Block) {
        setFloat(variables, USE_ITEM_INTERVAL_PROGRESS, 0.0f);
        setFloat(variables, USE_ITEM_STARTUP_PROGRESS, 0.0f);
        setFloat(variables, OFFHAND_USE_ITEM_INTERVAL_PROGRESS, 0.0f);
        setFloat(variables, OFFHAND_USE_ITEM_STARTUP_PROGRESS, 0.0f);
        return;
    }

    if (!usingOffhandItem) {
        setFloat(variables, OFFHAND_USE_ITEM_INTERVAL_PROGRESS, 0.0f);
        setFloat(variables, OFFHAND_USE_ITEM_STARTUP_PROGRESS, 0.0f);
        return;
    }

    // Only eating and drinking raise the arm through use_item_progress; other uses have their own pose.
    const float interval = getFloat(variables, USE_ITEM_INTERVAL_PROGRESS);
    const float startup = getFloat(variables, USE_ITEM_STARTUP_PROGRESS);
    const bool eating = isEatingAnimation(animation);
    setFloat(variables, OFFHAND_USE_ITEM_INTERVAL_PROGRESS, eating ? interval : 0.0f);
    setFloat(variables, OFFHAND_USE_ITEM_STARTUP_PROGRESS, eating ? startup : 0.0f);
    setFloat(variables, USE_ITEM_INTERVAL_PROGRESS, eating && (interval > 0.0f || startup > 0.0f) ? USE_ITEM_PROGRESS_PLAYING : 0.0f);
    setFloat(variables, USE_ITEM_STARTUP_PROGRESS, 0.0f);

    // LegacyMolangVariableUpdate works these out from the main hand item. The poses they pick are mirrored onto the
    // left arm by variable.offhand_pose.
    const float charge = std::clamp((maxDuration - remaining) / TRIDENT_THROW_THRESHOLD_TIME, 0.0f, 1.0f);
    setFloat(variables, CHARGE_AMOUNT, animation == UseAnimation::Spear ? charge : 0.0f);
    setFloat(variables, IS_BRANDISHING_SPEAR, animation == UseAnimation::Spear ? 1.0f : 0.0f);
    setFloat(variables, IS_HOLDING_SPYGLASS, animation == UseAnimation::Spyglass ? 1.0f : 0.0f);
    setFloat(variables, IS_TOOTING_GOAT_HORN, animation == UseAnimation::GoatHorn ? 1.0f : 0.0f);
    setFloat(variables, IS_USING_BRUSH, animation == UseAnimation::Brush ? 1.0f : 0.0f);
}

// Client: every player's third and first person model, the local player's included.
Amethyst::InlineHook<decltype(&LegacyMolangVariableUpdate::updateEntitySpecificMolangVariables)> _LegacyMolangVariableUpdate_updateEntitySpecificMolangVariables;

void LegacyMolangVariableUpdate_updateEntitySpecificMolangVariables(Actor& actor, RenderParams& renderParams)
{
    _LegacyMolangVariableUpdate_updateEntitySpecificMolangVariables(actor, renderParams);

    if (!actor.isPlayer())
        return;

    Player& player = static_cast<Player&>(actor);
    MolangVariableMap& variables = actor.mMolangVariables;
    updateSwing(player, variables);
    updateItemUse(player, variables);
}

} // namespace

void OffhandPlayerModel::Initialize()
{
    auto& hooks = Amethyst::GetHookManager();
    HOOK(LegacyMolangVariableUpdate, updateEntitySpecificMolangVariables);
}
