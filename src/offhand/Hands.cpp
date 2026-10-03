#include "offhand/Hands.hpp"

#include <world/ContainerID.hpp>
#include <world/SimpleContainer.hpp>
#include <world/actor/player/Player.hpp>
#include <world/actor/player/PlayerItemInUse.hpp>
#include <world/actor/provider/ActorEquipment.hpp>
#include <world/inventory/transaction/ComplexInventoryTransaction.hpp>
#include <world/inventory/transaction/ItemReleaseInventoryTransaction.hpp>
#include <world/inventory/transaction/ItemUseInventoryTransaction.hpp>
#include <world/inventory/transaction/ItemUseOnActorInventoryTransaction.hpp>
#include <world/item/ItemStack.hpp>

SimpleContainer& Offhand::getHandContainer(Player& player)
{
    return ActorEquipment::getHandContainer(player.getEntity());
}

const ItemStack& Offhand::getItem(Player& player)
{
    return getHandContainer(player).getItem(HAND_CONTAINER_SLOT);
}

bool Offhand::hasItem(const ItemStack& stack)
{
    return stack.mValid_DeprecatedSeeComment && stack.getItem() != nullptr && !stack.isNull() && stack.mCount != 0;
}

bool Offhand::isUsingItem(const Player& player)
{
    return !player.mItemInUse.mItem.isNull();
}

bool Offhand::isUsingOffhandItem(const Player& player)
{
    return isUsingItem(player) && player.mItemInUse.mSlot.mContainerId == ContainerID::CONTAINER_ID_OFFHAND;
}

void Offhand::setItemInUseSlotToOffhand(Player& player)
{
    player.mItemInUse.mSlot.mSlot = OFFHAND_CONTAINER_SLOT;
    player.mItemInUse.mSlot.mContainerId = ContainerID::CONTAINER_ID_OFFHAND;
}

void Offhand::markTransaction(ComplexInventoryTransaction& transaction)
{
    switch (transaction.mType) {
    case ComplexInventoryTransaction::Type::ItemUseTransaction:
        static_cast<ItemUseInventoryTransaction&>(transaction).mSlot = TRANSACTION_SLOT;
        break;
    case ComplexInventoryTransaction::Type::ItemUseOnEntityTransaction:
        static_cast<ItemUseOnActorInventoryTransaction&>(transaction).mSlot = TRANSACTION_SLOT;
        break;
    case ComplexInventoryTransaction::Type::ItemReleaseTransaction:
        static_cast<ItemReleaseInventoryTransaction&>(transaction).mSlot = TRANSACTION_SLOT;
        break;
    default:
        break;
    }
}
