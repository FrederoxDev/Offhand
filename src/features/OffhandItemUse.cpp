#include "features/OffhandItemUse.hpp"

#include "offhand/HandSwapScope.hpp"
#include "offhand/Hands.hpp"

#include <amethyst/runtime/ModContext.hpp>
#include <amethyst/runtime/utility/InlineHook.hpp>
#include <entity/components/ItemInUseComponent.hpp>
#include <SharedTypes/legacy/item/UseAnimation.hpp>
#include <world/actor/player/Player.hpp>
#include <world/actor/player/PlayerItemInUse.hpp>
#include <world/gamemode/GameMode.hpp>
#include <world/item/Item.hpp>
#include <world/item/ItemStack.hpp>

#include <cstdint>

namespace {

using SharedTypes::Legacy::UseAnimation;

// Remaining-duration window and interval at which Player::normalTick sends feeding particles.
constexpr int FEED_PARTICLE_DURATION = 26;
constexpr int FEED_PARTICLE_INTERVAL = 4;

bool hasFeedingAnimation(UseAnimation animation)
{
    return animation == UseAnimation::Eat
        || animation == UseAnimation::Drink
        || animation == UseAnimation::GlowStick
        || animation == UseAnimation::Sparkler;
}

// Player::normalTick's item-in-use block, checked against the offhand rather than the selected slot.
void tickOffhandItemInUse(Player& player)
{
    PlayerItemInUse& itemInUse = player.mItemInUse;
    const ItemStack& offhandItem = Offhand::getItem(player);

    if (!Offhand::hasItem(offhandItem) || !offhandItem.sameItem(itemInUse.mItem, ItemStackBase::COMPARISONOPTIONS_RELEVANTUSERDATA)) {
        player.stopUsingItem();
        return;
    }

    if (offhandItem.mCount != itemInUse.mItem.mCount || !offhandItem.matchesItem(itemInUse.mItem))
        itemInUse.mItem = offhandItem;

    const bool clientSide = player.isClientSide();
    const ItemInUseComponent* component = player.getEntity().tryGetComponent<ItemInUseComponent>();
    const Item* item = offhandItem.getItem();

    if (clientSide && component != nullptr && component->mDuration < FEED_PARTICLE_DURATION
        && component->mDuration % FEED_PARTICLE_INTERVAL == 0 && hasFeedingAnimation(item->mUseAnim)) {
        player.feed(offhandItem.getIdAux());
    }

    if (component != nullptr && component->mDuration != 0)
        return;

    if (clientSide && item->isFood())
        player.eat(offhandItem);

    Offhand::HandSwapScope scope(player);
    player.completeUsingItem();
}

Amethyst::InlineHook<decltype(&Player::normalTick)> _Player_normalTick;

void Player_normalTick(Player* self)
{
    if (!Offhand::isUsingOffhandItem(*self) || Offhand::HandSwapScope::isActive(*self)) {
        _Player_normalTick(self);
        return;
    }

    // Player::normalTick stops any use whose item is not in the selected slot. A zero count hides the item from
    // that check alone: isUsingItem and getItemInUse go through isNull, which does not look at the count.
    ItemStack& itemInUse = self->mItemInUse.mItem;
    const uint8_t count = itemInUse.mCount;
    itemInUse.mCount = 0;

    _Player_normalTick(self);

    if (!Offhand::isUsingOffhandItem(*self))
        return;

    itemInUse.mCount = count;
    tickOffhandItemInUse(*self);
}

Amethyst::InlineHook<decltype(&GameMode::releaseUsingItem)> _GameMode_releaseUsingItem;

void GameMode_releaseUsingItem(GameMode* self)
{
    Player& player = self->mPlayer;
    if (Offhand::HandSwapScope::isActive(player) || !Offhand::isUsingOffhandItem(player)) {
        _GameMode_releaseUsingItem(self);
        return;
    }

    Offhand::HandSwapScope scope(player);
    _GameMode_releaseUsingItem(self);
}

} // namespace

void OffhandItemUse::Initialize()
{
    auto& hooks = Amethyst::GetHookManager();
    HOOK_DIRECT_VIRTUAL(Player, normalTick);
    HOOK_DIRECT_VIRTUAL(GameMode, releaseUsingItem);
}
