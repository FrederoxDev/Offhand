#pragma once

#include <world/item/ItemStack.hpp>

class Container;
class Inventory;
class Player;
class SimpleContainer;

namespace Offhand {

// Vanilla's held-item paths all read and write the selected slot, so swapping it with the offhand makes them act on
// the offhand.
//
// Nothing outside the scope sees the swap: change notifications for both slots are held back, and on exit only a slot
// whose content actually changed is reported. On the server, an inventory resend requested inside the scope is sent
// after the hands are restored.
class HandSwapScope {
public:
    explicit HandSwapScope(Player& player);
    ~HandSwapScope();

    HandSwapScope(const HandSwapScope&) = delete;
    HandSwapScope& operator=(const HandSwapScope&) = delete;

    bool isSwapped() const { return mSwapped; }

    static bool isActive(const Player& player);

    // True for either hand slot of an active scope.
    static bool holdsNotification(const Container& container, int slot);

    // When true, the player's scope sends the inventory as it ends.
    static bool deferInventorySend(const Player& player, bool shouldSelectSlot);

private:
    Player& mPlayer;
    HandSwapScope* mOuter;
    Inventory* mInventory = nullptr;
    SimpleContainer* mHand = nullptr;
    int mSelectedSlot = 0;
    bool mSwapped = false;
    bool mWasUsingItem = false;
    bool mWasUsingOffhandItem = false;
    bool mInventorySendDeferred = false;
    bool mDeferredSendSelectsSlot = false;
    ItemStack mMainhandBefore;
    ItemStack mOffhandBefore;
};

} // namespace Offhand
