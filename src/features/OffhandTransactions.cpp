#include "features/OffhandTransactions.hpp"

#include "offhand/HandSwapScope.hpp"
#include "offhand/Hands.hpp"

#include <amethyst/runtime/ModContext.hpp>
#include <amethyst/runtime/utility/InlineHook.hpp>
#include <network/packet/LegacySetSlot.hpp>
#include <world/actor/player/Player.hpp>
#include <world/actor/player/PlayerInventory.hpp>
#include <world/containers/ContainerEnum.hpp>
#include <world/inventory/network/ItemStackNetManagerServer.hpp>
#include <world/inventory/transaction/InventoryTransaction.hpp>
#include <world/inventory/transaction/ItemReleaseInventoryTransaction.hpp>
#include <world/inventory/transaction/ItemUseInventoryTransaction.hpp>
#include <world/inventory/transaction/ItemUseOnActorInventoryTransaction.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

// The client lists an offhand transaction's changed slots with its hands swapped: the offhand's change under the
// selected slot and vice versa. _handleLegacyTransactionRequest runs after handle has restored the hands, so unmapped,
// each slot would get the other hand's net id and the client's next ItemStackRequest for that item would be rejected.
// Server thread only.
struct SwappedLegacySlots {
    const Player* player = nullptr;
    int selectedSlot = 0;
};

thread_local SwappedLegacySlots swappedLegacySlots;

// Vanilla expects the selected slot in mSlot.
template <typename Transaction, typename Original>
InventoryTransactionError handleForHand(const Transaction* self, Player& player, bool isSenderAuthority, Original& original)
{
    swappedLegacySlots = {};
    if (self->mSlot != Offhand::TRANSACTION_SLOT)
        return original(self, player, isSenderAuthority);

    int& slot = const_cast<Transaction*>(self)->mSlot;
    slot = player.getSupplies().mSelected;

    InventoryTransactionError error;
    {
        Offhand::HandSwapScope scope(player);
        error = original(self, player, isSenderAuthority);
    }

    // _handleLegacyTransactionRequest only follows a transaction that succeeded.
    if (error == InventoryTransactionError::NoError)
        swappedLegacySlots = {&player, slot};

    slot = Offhand::TRANSACTION_SLOT;
    return error;
}

Amethyst::InlineHook<InventoryTransactionError (ItemUseInventoryTransaction::*)(Player&, bool) const> _ItemUseInventoryTransaction_handle;

InventoryTransactionError ItemUseInventoryTransaction_handle(const ItemUseInventoryTransaction* self, Player& player, bool isSenderAuthority)
{
    return handleForHand(self, player, isSenderAuthority, _ItemUseInventoryTransaction_handle);
}

Amethyst::InlineHook<InventoryTransactionError (ItemUseOnActorInventoryTransaction::*)(Player&, bool) const> _ItemUseOnActorInventoryTransaction_handle;

InventoryTransactionError ItemUseOnActorInventoryTransaction_handle(const ItemUseOnActorInventoryTransaction* self, Player& player, bool isSenderAuthority)
{
    return handleForHand(self, player, isSenderAuthority, _ItemUseOnActorInventoryTransaction_handle);
}

Amethyst::InlineHook<InventoryTransactionError (ItemReleaseInventoryTransaction::*)(Player&, bool) const> _ItemReleaseInventoryTransaction_handle;

InventoryTransactionError ItemReleaseInventoryTransaction_handle(const ItemReleaseInventoryTransaction* self, Player& player, bool isSenderAuthority)
{
    return handleForHand(self, player, isSenderAuthority, _ItemReleaseInventoryTransaction_handle);
}

using LegacyRequestId = TypedClientNetId<ItemStackLegacyRequestIdTag, int, 0>;

Amethyst::InlineHook<decltype(&ItemStackNetManagerServer::_handleLegacyTransactionRequest)> _ItemStackNetManagerServer__handleLegacyTransactionRequest;

// Legacy set slots address the offhand as Offhand::HAND_CONTAINER_SLOT, not OFFHAND_CONTAINER_SLOT.
void ItemStackNetManagerServer__handleLegacyTransactionRequest(ItemStackNetManagerServer* self, const LegacyRequestId& legacyClientRequestId, const std::vector<LegacySetSlot>& legacySetItemSlots)
{
    const SwappedLegacySlots swapped = std::exchange(swappedLegacySlots, {});
    if (swapped.player != &self->mPlayer) {
        _ItemStackNetManagerServer__handleLegacyTransactionRequest(self, legacyClientRequestId, legacySetItemSlots);
        return;
    }

    std::vector<LegacySetSlot> unswapped;
    const auto add = [&unswapped](ContainerEnumName container, uint8_t slot) {
        auto entry = std::find_if(unswapped.begin(), unswapped.end(), [container](const LegacySetSlot& candidate) {
            return candidate.mContainerEnum == container;
        });
        if (entry == unswapped.end())
            entry = unswapped.insert(unswapped.end(), LegacySetSlot{container, {}});
        entry->mSlots.push_back(slot);
    };

    for (const LegacySetSlot& entry : legacySetItemSlots) {
        for (const uint8_t slot : entry.mSlots) {
            if (entry.mContainerEnum == ContainerEnumName::InventoryContainer && slot == swapped.selectedSlot)
                add(ContainerEnumName::OffhandContainer, static_cast<uint8_t>(Offhand::HAND_CONTAINER_SLOT));
            else if (entry.mContainerEnum == ContainerEnumName::OffhandContainer && slot == Offhand::HAND_CONTAINER_SLOT)
                add(ContainerEnumName::InventoryContainer, static_cast<uint8_t>(swapped.selectedSlot));
            else
                add(entry.mContainerEnum, slot);
        }
    }

    _ItemStackNetManagerServer__handleLegacyTransactionRequest(self, legacyClientRequestId, unswapped);
}

} // namespace

void OffhandTransactions::Initialize()
{
    auto& hooks = Amethyst::GetHookManager();
    HOOK_DIRECT_VIRTUAL(ItemUseInventoryTransaction, handle);
    HOOK_DIRECT_VIRTUAL(ItemUseOnActorInventoryTransaction, handle);
    HOOK_DIRECT_VIRTUAL(ItemReleaseInventoryTransaction, handle);
    HOOK(ItemStackNetManagerServer, _handleLegacyTransactionRequest);
}
