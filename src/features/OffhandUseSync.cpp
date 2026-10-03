#include "features/OffhandUseSync.hpp"

#include "offhand/HandSwapScope.hpp"
#include "offhand/Hands.hpp"

#include <amethyst/runtime/ModContext.hpp>
#include <amethyst/runtime/networking/NetworkManager.hpp>
#include <amethyst/runtime/utility/InlineHook.hpp>
#include <client/game/ClientInstance.hpp>
#include <client/player/LocalPlayer.hpp>
#include <SharedTypes/legacy/ActorUniqueID.hpp>
#include <world/actor/player/Player.hpp>
#include <world/actor/player/PlayerInventory.hpp>
#include <world/item/Item.hpp>
#include <world/item/ItemStack.hpp>
#include <world/level/ILevel.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace {

// Other clients only receive ActorFlags::USINGITEM and start the use from the selected slot. Java's
// DATA_LIVING_ENTITY_FLAGS also carries the hand; this packet adds it for every item use the server starts.
class ItemUseHandPacket : public Amethyst::CustomPacket {
public:
    uint64_t mRuntimeId = 0;
    bool mOffhand = false;

    std::string getName() const override
    {
        return "ItemUseHandPacket";
    }

    void write(BinaryStream& out) const override
    {
        out.writeUnsignedVarInt64(mRuntimeId, "runtimeId", "");
        out.writeBool(mOffhand, "offhand", "");
    }

    Bedrock::Result<void, std::error_code> read(ReadOnlyBinaryStream& in) override
    {
        auto runtimeId = in.getUnsignedVarInt64();
        if (!runtimeId.has_value())
            return ::nonstd::expected_lite::make_unexpected(runtimeId.error());

        auto offhand = in.getBool();
        if (!offhand.has_value())
            return ::nonstd::expected_lite::make_unexpected(offhand.error());

        mRuntimeId = *runtimeId;
        mOffhand = *offhand;
        return {};
    }
};

// Keyed by runtime ID, the local player included. Client thread only, so unlocked.
std::unordered_map<uint64_t, bool> serverOffhandUse;

LocalPlayer* getLocalPlayer()
{
    ClientInstance* client = Amethyst::GetClientCtx().mClientInstance;
    return client != nullptr ? client->getLocalPlayer() : nullptr;
}

// ItemInUseComponentRemoveSystem and RemotePlayer::normalTick start a use from ActorFlags::USINGITEM, handing over the
// selected slot itself. Uses started by input hand over a copy of the item.
bool isStartedFromUsingItemFlag(Player& player, const ItemStack& instance)
{
    return &instance == &player.getSupplies().getSelectedItem();
}

// LivingEntity#onSyncedDataUpdated, DATA_LIVING_ENTITY_FLAGS.
class ItemUseHandPacketHandler : public Amethyst::CustomPacketHandler {
public:
    void handle(const NetworkIdentifier&, NetEventCallback&, const Amethyst::CustomPacket& packet) const override
    {
        LocalPlayer* localPlayer = getLocalPlayer();
        if (localPlayer == nullptr || localPlayer->mLevel == nullptr)
            return;

        const auto& handPacket = static_cast<const ItemUseHandPacket&>(packet);
        serverOffhandUse[handPacket.mRuntimeId] = handPacket.mOffhand;

        Actor* actor = localPlayer->mLevel->getRuntimeEntity(ActorRuntimeID(handPacket.mRuntimeId), false);
        if (actor == nullptr || actor == localPlayer || !actor->isPlayer())
            return;

        // A use the flag already started with the wrong hand is restarted with the right one on the player's next tick.
        Player& player = static_cast<Player&>(*actor);
        if (Offhand::isUsingItem(player) && Offhand::isUsingOffhandItem(player) != handPacket.mOffhand)
            player.stopUsingItem();
    }
};

Amethyst::InlineHook<decltype(&Player::startUsingItem)> _Player_startUsingItem;

void Player_startUsingItem(Player* self, const ItemStack& instance, int duration)
{
    // Every use started inside a HandSwapScope is an offhand one.
    if (!self->isClientSide()) {
        _Player_startUsingItem(self, instance, duration);
        if (!Offhand::isUsingItem(*self))
            return;

        auto packet = std::make_unique<ItemUseHandPacket>();
        packet->mRuntimeId = self->getRuntimeID().rawID;
        packet->mOffhand = Offhand::HandSwapScope::isActive(*self);
        Amethyst::GetNetworkManager().SendBroadcast(self->mPacketSender, std::move(packet));
        return;
    }

    // A use started from ActorFlags::USINGITEM (another player's, or the local player's when the flag restarts a use the
    // client already finished) has to come from the hand the server is using.
    if (!isStartedFromUsingItemFlag(*self, instance)) {
        _Player_startUsingItem(self, instance, duration);
        return;
    }

    const auto hand = serverOffhandUse.find(self->getRuntimeID().rawID);
    if (hand == serverOffhandUse.end() || !hand->second) {
        _Player_startUsingItem(self, instance, duration);
        return;
    }

    const ItemStack offhandItem = Offhand::getItem(*self);
    const Item* offhandType = offhandItem.getItem();
    if (!Offhand::hasItem(offhandItem) || offhandType == nullptr)
        return;

    _Player_startUsingItem(self, offhandItem, offhandType->getMaxUseDuration(&offhandItem));
    if (Offhand::isUsingItem(*self))
        Offhand::setItemInUseSlotToOffhand(*self);
}

} // namespace

void OffhandUseSync::Initialize()
{
    Amethyst::GetNetworkManager().RegisterPacketType<ItemUseHandPacket>(std::make_unique<ItemUseHandPacketHandler>());

    auto& hooks = Amethyst::GetHookManager();
    HOOK(Player, startUsingItem);
}
