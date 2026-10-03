#pragma once

class ComplexInventoryTransaction;
class ItemStack;
class Player;
class SimpleContainer;

namespace Offhand {

// HandSlot::Offhand
inline constexpr int HAND_CONTAINER_SLOT = 1;

// The offhand's slot when the hand container is addressed as CONTAINER_ID_OFFHAND.
inline constexpr int OFFHAND_CONTAINER_SLOT = 0;

// Put in an item transaction's selected-slot field to mark it as made with the offhand.
inline constexpr int TRANSACTION_SLOT = -2;

SimpleContainer& getHandContainer(Player& player);

const ItemStack& getItem(Player& player);

// ItemStackBase::operator bool, which the import library does not provide.
bool hasItem(const ItemStack& stack);

bool isUsingItem(const Player& player);

bool isUsingOffhandItem(const Player& player);

void setItemInUseSlotToOffhand(Player& player);

// For a transaction made inside a HandSwapScope, so the receiving side handles it with the hands swapped too.
void markTransaction(ComplexInventoryTransaction& transaction);

} // namespace Offhand
