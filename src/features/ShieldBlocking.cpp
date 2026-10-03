#include "features/ShieldBlocking.hpp"

#include "offhand/Hands.hpp"

#include <amethyst/runtime/ModContext.hpp>
#include <amethyst/runtime/utility/InlineHook.hpp>
#include <Core/String/StringHash.hpp>
#include <server/ServerPlayer.hpp>
#include <SharedTypes/legacy/item/UseAnimation.hpp>
#include <SharedTypes/world/actor/ActorFlags.hpp>
#include <utilities/SynchedActorDataAccess.hpp>
#include <world/actor/player/Player.hpp>
#include <world/actor/player/PlayerItemInUse.hpp>
#include <world/gamemode/GameMode.hpp>
#include <world/item/Item.hpp>
#include <world/item/ItemStack.hpp>
#include <world/level/Tick.hpp>

#include <cstdint>

namespace {

using SharedTypes::Legacy::UseAnimation;

// ShieldItem#getUseDuration. OffhandItems gives shields the same, for uses vanilla starts itself.
constexpr int SHIELD_USE_DURATION = 72000;

bool isShield(const Item* item)
{
    return item != nullptr && item->mUseAnim == UseAnimation::Block;
}

bool isOnCooldown(const Player& player, const Item& item)
{
    return player.isItemOnCooldown(item.getCooldownCategory());
}

// LivingEntity#isBlocking without its five tick delay, which Player::isBlocking already applies through
// ShieldItem::EFFECTIVE_BLOCK_DELAY.
bool isUsingShield(const Player& player)
{
    if (!Offhand::isUsingItem(player))
        return false;

    const Item* item = player.mItemInUse.mItem.getItem();
    return isShield(item) && !isOnCooldown(player, *item);
}

// Both sides: reached from GameMode::baseUseItem on the client and through the use transaction on the server. Java's
// ShieldItem#use starts using the shield from either hand; ShieldItem::use does nothing, as Bedrock blocks by sneaking.
Amethyst::InlineHook<decltype(&GameMode::useItem)> _GameMode_useItem;

bool GameMode_useItem(GameMode* self, ItemStack& item)
{
    const bool used = _GameMode_useItem(self, item);

    Player& player = self->mPlayer;
    const Item* type = item.getItem();
    if (isShield(type) && Offhand::hasItem(item) && !Offhand::isUsingItem(player) && !isOnCooldown(player, *type))
        player.startUsingItem(item, SHIELD_USE_DURATION);

    return used;
}

// ServerPlayer::normalTick sets BLOCKING while sneaking or riding. Java blocks only while a shield is being used, so
// the flags are recomputed from that.
Amethyst::InlineHook<decltype(&ServerPlayer::normalTick)> _ServerPlayer_normalTick;

void ServerPlayer_normalTick(ServerPlayer* self)
{
    EntityContext& entity = self->getEntity();
    const bool wasBlocking = SynchedActorDataAccess::getActorFlag(entity, ActorFlags::BLOCKING);
    const uint64_t previousBlockingTick = self->mPrevShieldBlockingTick.tickID;

    _ServerPlayer_normalTick(self);

    const ItemStack& shield = self->getCurrentActiveShield();
    const bool shieldRaisedChanged = Offhand::hasItem(shield) && shield.mBlockingTick.tickID != previousBlockingTick;
    const bool blocking = isUsingShield(*self);

    SynchedActorDataAccess::setActorFlag(entity, ActorFlags::TRANSITION_BLOCKING, shieldRaisedChanged || (blocking && !wasBlocking));
    SynchedActorDataAccess::setActorFlag(entity, ActorFlags::BLOCKING, blocking);
}

// Both sides: Java's Player#disableShield also stops using the shield. Player::tryDisableShield only starts the
// cooldown, which the client also receives, so each side stops its own use when that cooldown starts.
Amethyst::InlineHook<void (Player::*)(const HashedString&, int, bool)> _Player_startItemCooldown;

void Player_startItemCooldown(Player* self, const HashedString& type, int tickDuration, bool updateClient)
{
    _Player_startItemCooldown(self, type, tickDuration, updateClient);

    if (!Offhand::isUsingItem(*self))
        return;

    const Item* item = self->mItemInUse.mItem.getItem();
    if (isShield(item) && isOnCooldown(*self, *item))
        self->stopUsingItem();
}

} // namespace

void ShieldBlocking::Initialize()
{
    auto& hooks = Amethyst::GetHookManager();
    HOOK_DIRECT_VIRTUAL(GameMode, useItem);
    HOOK_DIRECT_VIRTUAL(ServerPlayer, normalTick);
    HOOK(Player, startItemCooldown);
}
