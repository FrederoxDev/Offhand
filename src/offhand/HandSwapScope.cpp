#include "offhand/HandSwapScope.hpp"

#include "offhand/Hands.hpp"

#include <world/Container.hpp>
#include <world/ContainerID.hpp>
#include <world/SimpleContainer.hpp>
#include <world/actor/player/Inventory.hpp>
#include <world/actor/player/Player.hpp>
#include <world/actor/player/PlayerInventory.hpp>
#include <world/actor/player/PlayerItemInUse.hpp>

#include <vector>

namespace {

// Scopes for different players can nest, so every query walks the chain.
thread_local Offhand::HandSwapScope* activeScope = nullptr;

void exchange(ItemStack& lhs, ItemStack& rhs)
{
    ItemStack temporary = lhs;
    lhs = rhs;
    rhs = temporary;
}

} // namespace

Offhand::HandSwapScope::HandSwapScope(Player& player)
    : mPlayer(player)
    , mOuter(activeScope)
{
    if (isActive(player))
        return;

    PlayerInventory& supplies = player.getSupplies();
    if (supplies.mSelectedContainerId != ContainerID::CONTAINER_ID_INVENTORY)
        return;

    Inventory& inventory = *supplies.mInventory;
    SimpleContainer& hand = getHandContainer(player);
    std::vector<ItemStack>& inventoryItems = inventory.mItems;
    std::vector<ItemStack>& handItems = hand.mItems;
    mSelectedSlot = supplies.mSelected;
    if (mSelectedSlot < 0 || mSelectedSlot >= static_cast<int>(inventoryItems.size()) || HAND_CONTAINER_SLOT >= static_cast<int>(handItems.size()))
        return;

    mInventory = &inventory;
    mHand = &hand;
    mMainhandBefore = inventoryItems[mSelectedSlot];
    mOffhandBefore = handItems[HAND_CONTAINER_SLOT];
    exchange(inventoryItems[mSelectedSlot], handItems[HAND_CONTAINER_SLOT]);

    mWasUsingItem = Offhand::isUsingItem(player);
    mWasUsingOffhandItem = Offhand::isUsingOffhandItem(player);
    if (mWasUsingOffhandItem) {
        player.mItemInUse.mSlot.mSlot = mSelectedSlot;
        player.mItemInUse.mSlot.mContainerId = ContainerID::CONTAINER_ID_INVENTORY;
    }

    mSwapped = true;
    activeScope = this;
}

Offhand::HandSwapScope::~HandSwapScope()
{
    if (!mSwapped)
        return;

    std::vector<ItemStack>& inventoryItems = mInventory->mItems;
    std::vector<ItemStack>& handItems = mHand->mItems;
    exchange(inventoryItems[mSelectedSlot], handItems[HAND_CONTAINER_SLOT]);

    PlayerItemInUse& itemInUse = mPlayer.mItemInUse;
    const bool usingSelectedSlot = Offhand::isUsingItem(mPlayer)
        && itemInUse.mSlot.mContainerId == ContainerID::CONTAINER_ID_INVENTORY
        && itemInUse.mSlot.mSlot == mSelectedSlot;
    if (usingSelectedSlot && (mWasUsingOffhandItem || !mWasUsingItem))
        setItemInUseSlotToOffhand(mPlayer);

    activeScope = mOuter;

    // Listeners hear only the net change to each hand, never the swap itself.
    if (inventoryItems[mSelectedSlot] != mMainhandBefore)
        mInventory->setContainerChanged(mSelectedSlot);
    if (handItems[HAND_CONTAINER_SLOT] != mOffhandBefore)
        mHand->setContainerChanged(HAND_CONTAINER_SLOT);

    if (mInventorySendDeferred)
        mPlayer.sendInventory(mDeferredSendSelectsSlot);
}

bool Offhand::HandSwapScope::isActive(const Player& player)
{
    for (const HandSwapScope* scope = activeScope; scope != nullptr; scope = scope->mOuter) {
        if (&scope->mPlayer == &player)
            return true;
    }
    return false;
}

bool Offhand::HandSwapScope::holdsNotification(const Container& container, int slot)
{
    for (const HandSwapScope* scope = activeScope; scope != nullptr; scope = scope->mOuter) {
        if ((&container == static_cast<const Container*>(scope->mInventory) && slot == scope->mSelectedSlot)
            || (&container == static_cast<const Container*>(scope->mHand) && slot == HAND_CONTAINER_SLOT))
            return true;
    }
    return false;
}

bool Offhand::HandSwapScope::deferInventorySend(const Player& player, bool shouldSelectSlot)
{
    for (HandSwapScope* scope = activeScope; scope != nullptr; scope = scope->mOuter) {
        if (&scope->mPlayer != &player)
            continue;

        scope->mInventorySendDeferred = true;
        scope->mDeferredSendSelectsSlot = scope->mDeferredSendSelectsSlot || shouldSelectSlot;
        return true;
    }
    return false;
}
