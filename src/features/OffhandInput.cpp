#include "features/OffhandInput.hpp"

#include "features/OffhandSwing.hpp"
#include "offhand/HandSwapScope.hpp"
#include "offhand/Hands.hpp"
#include "offhand/InteractingHand.hpp"

#include <amethyst/runtime/ModContext.hpp>
#include <amethyst/runtime/utility/InlineHook.hpp>
#include <client/game/ClientInputCallbacks.hpp>
#include <client/game/IClientInstance.hpp>
#include <client/input/BuildActionIntention.hpp>
#include <client/player/LocalPlayer.hpp>
#include <client/renderer/game/ItemInHandRenderer.hpp>
#include <Core/Math/Vec3.hpp>
#include <fallback/globals/VanillaItemNames.hpp>
#include <SharedTypes/world/level/BlockPos.hpp>
#include <world/actor/ActorSwingSource.hpp>
#include <world/actor/player/PlayerInventory.hpp>
#include <world/actor/player/PlayerItemInUse.hpp>
#include <world/actor/provider/PlayerMoveInput.hpp>
#include <world/gamemode/GameMode.hpp>
#include <world/inventory/transaction/ComplexInventoryTransaction.hpp>
#include <world/item/Item.hpp>
#include <world/item/ItemStack.hpp>
#include <world/level/BlockSource.hpp>
#include <world/level/block/Block.hpp>
#include <world/level/material/Material.hpp>
#include <world/phys/HitResult.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

namespace {

using namespace std::chrono_literals;

constexpr auto INTERACT_DELAY = 200ms;

constexpr int BAI_BUILD = 1;
constexpr int BAI_FIRSTBUILD = 32;
constexpr int BAI_INTERACT = 16;
constexpr int BAI_FIRSTINTERACT = 128;

constexpr int BUILD_INTENT = BAI_BUILD | BAI_FIRSTBUILD;
constexpr int USE_INTENT = BUILD_INTENT | BAI_INTERACT | BAI_FIRSTINTERACT;

// What ClientInputCallbacks::handleBuildAction did with the main hand, which decides whether Java's
// Minecraft#startUseItem would go on to the offhand.
struct MainhandAttempt {
    Actor* interactTarget = nullptr;
    Vec3 interactLocation;
    bool interacted = false;
    bool built = false;
    bool used = false;
};

thread_local MainhandAttempt* mainhandAttempt = nullptr;
thread_local bool offhandBuildResult = false;

bool isSecondaryUseActive(Player& player)
{
    return PlayerMoveInput::isSneakDown(player.getEntity()) || PlayerMoveInput::isDescending(player.getEntity());
}

// MultiPlayerGameMode#performUseItemOn skips the block when the player is secondary-using with anything in either
// hand. GameMode::useItemOn only checks the main hand, so an empty main hand would still use the block.
bool skipsMainhandBlockUse(Player& player)
{
    return isSecondaryUseActive(player)
        && !Offhand::hasItem(player.getSupplies().getSelectedItem())
        && Offhand::hasItem(Offhand::getItem(player));
}

// The OFF_HAND iteration of Minecraft#startUseItem, through the same GameMode calls handleBuildAction makes for the
// main hand. A value overrides handleBuildAction's own answer to whether the in-progress intention is reset.
std::optional<bool> useOffhand(IClientInstance& client, LocalPlayer& player, int intent, const MainhandAttempt& attempt, const HitResult& solidHitResult, const HitResult& liquidHitResult)
{
    GameMode& gameMode = *player.mGameMode;
    const auto now = std::chrono::steady_clock::now();

    if (solidHitResult.mType == HitResultType::ENTITY) {
        if (attempt.interactTarget != nullptr) {
            bool interacted = false;
            {
                Offhand::HandSwapScope scope(player);
                interacted = scope.isSwapped() && gameMode.interact(*attempt.interactTarget, attempt.interactLocation);
            }

            if (interacted) {
                client.setNoBlockBreakUntil(now + INTERACT_DELAY);
                client.clearInProgressBAI();
                return false;
            }
        }
    }
    else if ((intent & BUILD_INTENT) != 0) {
        const ItemStack& offhandItem = Offhand::getItem(player);
        const bool solidTile = solidHitResult.mType == HitResultType::TILE;

        if (solidTile || (liquidHitResult.mIsHitLiquid && offhandItem.isLiquidClipItem())) {
            const BlockPos& hit = solidTile ? solidHitResult.mBlock : liquidHitResult.mLiquid;
            const uint8_t facing = solidTile ? solidHitResult.mFacing : liquidHitResult.mLiquidFacing;

            BlockSource& region = player.getDimensionBlockSource();
            const BlockPos neighbor = hit.neighbor(facing);
            const bool neighborInHeightLimits = neighbor.y >= region.getMinHeight() && neighbor.y < region.getMaxHeight();
            if (!region.getBlock(hit).getMaterial().isLiquid() && !neighborInHeightLimits)
                return true;

            offhandBuildResult = false;
            {
                Offhand::HandSwapScope scope(player);
                if (scope.isSwapped())
                    gameMode.startBuildBlock(hit, facing);
            }

            if (offhandBuildResult)
                return std::nullopt;
        }
    }

    if ((intent & BAI_INTERACT) == 0)
        return std::nullopt;

    const ItemStack item = Offhand::getItem(player);
    client.setNoBlockBreakUntil(now + INTERACT_DELAY);

    bool used = false;
    {
        Offhand::HandSwapScope scope(player);
        if (scope.isSwapped()) {
            const ItemStack selected = player.getSupplies().getSelectedItem();
            used = gameMode.baseUseItem(selected);
        }
    }

    if (used) {
        if (ItemInHandRenderer* itemInHandRenderer = client.getItemInHandRenderer())
            itemInHandRenderer->mHeightOffHand = 0.0f;
    }

    const Item* itemType = item.getItem();
    const bool usable = itemType != nullptr && (itemType->isThrowable() || itemType->isUseable()) && item.mCount != 0;
    if (Offhand::isUsingItem(player) || usable) {
        client.resetBai(BAI_INTERACT);
        return false;
    }

    return std::nullopt;
}

// handleBuildAction keeps building while a main hand brush is in use, and BrushItem::_useOn restarts the use each
// time, which is how holding use keeps brushing.
void continueOffhandBrushing(LocalPlayer& player, const BuildActionIntention& bai, const HitResult& solidHitResult)
{
    if ((bai.mAction & BUILD_INTENT) == 0 || solidHitResult.mType != HitResultType::TILE)
        return;

    if (!player.mItemInUse.mItem.isInstance(VanillaItemNames::Brush, false))
        return;

    BlockSource& region = player.getDimensionBlockSource();
    const BlockPos neighbor = solidHitResult.mBlock.neighbor(solidHitResult.mFacing);
    const bool neighborInHeightLimits = neighbor.y >= region.getMinHeight() && neighbor.y < region.getMaxHeight();
    if (!region.getBlock(solidHitResult.mBlock).getMaterial().isLiquid() && !neighborInHeightLimits)
        return;

    Offhand::HandSwapScope scope(player);
    if (scope.isSwapped())
        player.mGameMode->startBuildBlock(solidHitResult.mBlock, solidHitResult.mFacing);
}

Amethyst::InlineHook<decltype(&ClientInputCallbacks::handleBuildAction)> _ClientInputCallbacks_handleBuildAction;

bool ClientInputCallbacks_handleBuildAction(IClientInstance& client, BuildActionIntention& bai, const HitResult& solidHitResult, const HitResult& liquidHitResult)
{
    LocalPlayer* player = client.getLocalPlayer();

    // Minecraft#handleKeybinds drops every attack and use click while an item is in use. handleBuildAction's
    // exceptions (fishing rod, brush, charged crossbow) only apply to a main hand item in use.
    if (player != nullptr && Offhand::isUsingOffhandItem(*player) && !Offhand::HandSwapScope::isActive(*player)) {
        continueOffhandBrushing(*player, bai, solidHitResult);
        return true;
    }

    if (player == nullptr || (bai.mAction & USE_INTENT) == 0 || Offhand::HandSwapScope::isActive(*player))
        return _ClientInputCallbacks_handleBuildAction(client, bai, solidHitResult, liquidHitResult);

    const int intent = bai.mAction;
    const bool canInteract = client.getNoBlockBreakUntil() < std::chrono::steady_clock::now();
    const bool wasUsingItem = Offhand::isUsingItem(*player);

    MainhandAttempt attempt;
    mainhandAttempt = &attempt;
    const bool resetBai = _ClientInputCallbacks_handleBuildAction(client, bai, solidHitResult, liquidHitResult);
    mainhandAttempt = nullptr;

    // The client's GameMode::interact succeeds whenever the server sent interact text, which is worked out with both
    // hands, so it can't tell whether the main hand interacted with an entity. The offhand asks too, and the server
    // turns it down when the main hand already interacted.
    const bool mainhandInteracted = attempt.interacted && solidHitResult.mType != HitResultType::ENTITY;
    const bool mainhandConsumed = mainhandInteracted || attempt.built || attempt.used || Offhand::isUsingItem(*player);
    if (!canInteract || wasUsingItem || mainhandConsumed || player->isSpectator() || !Offhand::hasItem(Offhand::getItem(*player)))
        return resetBai;

    return useOffhand(client, *player, intent, attempt, solidHitResult, liquidHitResult).value_or(resetBai);
}

Amethyst::InlineHook<decltype(&GameMode::buildBlock)> _GameMode_buildBlock;

bool GameMode_buildBlock(GameMode* self, const BlockPos& pos, uint8_t face, const bool isSimTick)
{
    Player& player = self->mPlayer;

    if (Offhand::HandSwapScope::isActive(player)) {
        offhandBuildResult = _GameMode_buildBlock(self, pos, face, isSimTick);
        return offhandBuildResult;
    }

    if (mainhandAttempt != nullptr && !isSimTick) {
        if (skipsMainhandBlockUse(player))
            return false;

        mainhandAttempt->built = _GameMode_buildBlock(self, pos, face, isSimTick);
        return mainhandAttempt->built;
    }

    if (_GameMode_buildBlock(self, pos, face, isSimTick))
        return true;

    if (!isSimTick || !Offhand::hasItem(Offhand::getItem(player)))
        return false;

    Offhand::HandSwapScope scope(player);
    return scope.isSwapped() && _GameMode_buildBlock(self, pos, face, isSimTick);
}

// Server: Java only tries the offhand when the main hand fails, so the offhand's ItemUseOnActorInventoryTransaction
// for the same click is turned down when the main hand already interacted with that entity.
struct MainhandInteraction {
    uint64_t entity = 0;
    bool interacted = false;
};

thread_local std::unordered_map<uint64_t, MainhandInteraction> mainhandInteractions;

Amethyst::InlineHook<decltype(&GameMode::interact)> _GameMode_interact;

// Both sides: the server handles every ItemUseOnActorInventoryTransaction through here too.
bool GameMode_interact(GameMode* self, Actor& entity, const Vec3& location)
{
    Player& player = self->mPlayer;
    Offhand::InteractingHandScope interactingHand(player);

    if (!player.isClientSide()) {
        const uint64_t playerId = player.getRuntimeID().rawID;
        const uint64_t entityId = entity.getRuntimeID().rawID;

        if (!Offhand::HandSwapScope::isActive(player)) {
            const bool interacted = _GameMode_interact(self, entity, location);
            mainhandInteractions[playerId] = {entityId, interacted};
            return interacted;
        }

        if (const auto mainhand = mainhandInteractions.find(playerId); mainhand != mainhandInteractions.end()) {
            const bool handled = mainhand->second.entity == entityId && mainhand->second.interacted;
            mainhandInteractions.erase(mainhand);
            if (handled)
                return false;
        }

        return _GameMode_interact(self, entity, location);
    }

    if (mainhandAttempt == nullptr || Offhand::HandSwapScope::isActive(player))
        return _GameMode_interact(self, entity, location);

    mainhandAttempt->interactTarget = &entity;
    mainhandAttempt->interactLocation = location;
    mainhandAttempt->interacted = _GameMode_interact(self, entity, location);
    return mainhandAttempt->interacted;
}

Amethyst::InlineHook<decltype(&GameMode::baseUseItem)> _GameMode_baseUseItem;

bool GameMode_baseUseItem(GameMode* self, const ItemStack& item)
{
    if (mainhandAttempt == nullptr || Offhand::HandSwapScope::isActive(self->mPlayer))
        return _GameMode_baseUseItem(self, item);

    mainhandAttempt->used = _GameMode_baseUseItem(self, item);
    return mainhandAttempt->used;
}

Amethyst::InlineHook<decltype(&LocalPlayer::sendComplexInventoryTransaction)> _LocalPlayer_sendComplexInventoryTransaction;

void LocalPlayer_sendComplexInventoryTransaction(const LocalPlayer* self, std::unique_ptr<ComplexInventoryTransaction> transaction)
{
    if (transaction != nullptr && Offhand::HandSwapScope::isActive(*self))
        Offhand::markTransaction(*transaction);

    _LocalPlayer_sendComplexInventoryTransaction(self, std::move(transaction));
}

Amethyst::InlineHook<decltype(&LocalPlayer::swing)> _LocalPlayer_swing;

bool LocalPlayer_swing(LocalPlayer* self, ActorSwingSource swingSource)
{
    if (!Offhand::HandSwapScope::isActive(*self))
        return _LocalPlayer_swing(self, swingSource);

    OffhandSwing::swingLocal(*self);
    return true;
}

} // namespace

void OffhandInput::Initialize()
{
    auto& hooks = Amethyst::GetHookManager();
    HOOK(ClientInputCallbacks, handleBuildAction);
    HOOK_DIRECT_VIRTUAL(GameMode, buildBlock);
    HOOK_DIRECT_VIRTUAL(GameMode, interact);
    HOOK(GameMode, baseUseItem);
    HOOK_DIRECT_VIRTUAL(LocalPlayer, sendComplexInventoryTransaction);
    HOOK_DIRECT_VIRTUAL(LocalPlayer, swing);
}
