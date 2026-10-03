#include "features/OffhandSync.hpp"

#include "offhand/HandSwapScope.hpp"
#include "offhand/Hands.hpp"

#include <amethyst/runtime/ModContext.hpp>
#include <amethyst/runtime/utility/InlineHook.hpp>
#include <server/ServerPlayer.hpp>
#include <world/Container.hpp>
#include <world/inventory/transaction/ComplexInventoryTransaction.hpp>

#include <memory>

namespace {

// Both sides. Listeners (HUD, container models, server item stack ids) must not see a HandSwapScope's swap; the scope
// reports the net change when it ends.
Amethyst::InlineHook<decltype(&Container::setContainerChanged)> _Container_setContainerChanged;

void Container_setContainerChanged(Container* self, int slot)
{
    if (Offhand::HandSwapScope::holdsNotification(*self, slot))
        return;

    _Container_setContainerChanged(self, slot);
}

// Server. Vanilla resends the inventory from inside calls a HandSwapScope wraps, which would show the client its hands
// swapped; the scope resends after restoring them.
Amethyst::InlineHook<decltype(&ServerPlayer::sendInventory)> _ServerPlayer_sendInventory;

void ServerPlayer_sendInventory(ServerPlayer* self, bool shouldSelectSlot)
{
    if (Offhand::HandSwapScope::deferInventorySend(*self, shouldSelectSlot))
        return;

    _ServerPlayer_sendInventory(self, shouldSelectSlot);
}

// Server. A transaction sent inside a HandSwapScope (e.g. Player::completeUsingItem's) is about the offhand, so the
// client must handle it with its hands swapped too.
Amethyst::InlineHook<decltype(&ServerPlayer::sendComplexInventoryTransaction)> _ServerPlayer_sendComplexInventoryTransaction;

void ServerPlayer_sendComplexInventoryTransaction(const ServerPlayer* self, std::unique_ptr<ComplexInventoryTransaction> transaction)
{
    if (transaction != nullptr && Offhand::HandSwapScope::isActive(*self))
        Offhand::markTransaction(*transaction);

    _ServerPlayer_sendComplexInventoryTransaction(self, std::move(transaction));
}

} // namespace

void OffhandSync::Initialize()
{
    auto& hooks = Amethyst::GetHookManager();
    HOOK_DIRECT_VIRTUAL(Container, setContainerChanged);
    HOOK_DIRECT_VIRTUAL(ServerPlayer, sendInventory);
    HOOK_DIRECT_VIRTUAL(ServerPlayer, sendComplexInventoryTransaction);
}
