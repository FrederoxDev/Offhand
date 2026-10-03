#include "features/OffhandSwing.hpp"

#include <amethyst/runtime/ModContext.hpp>
#include <amethyst/runtime/networking/NetworkManager.hpp>
#include <amethyst/runtime/utility/InlineHook.hpp>
#include <client/game/ClientInstance.hpp>
#include <client/player/LocalPlayer.hpp>
#include <entity/components/UserEntityIdentifierComponent.hpp>
#include <network/ServerNetworkHandler.hpp>
#include <server/ServerPlayer.hpp>
#include <SharedTypes/legacy/ActorUniqueID.hpp>
#include <SubClientId.hpp>
#include <world/actor/ActorSwingSource.hpp>
#include <world/actor/Mob.hpp>
#include <world/actor/player/Player.hpp>
#include <world/level/ILevel.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace {

// Mob keeps one swing for both hands, so this is the missing LivingEntity#swingingArm. Locked because the client and
// server threads both swing mobs.
std::mutex offhandSwingersMutex;
std::unordered_set<const Mob*> offhandSwingers;

void setOffhandSwing(const Mob& mob, bool offhand)
{
    std::scoped_lock lock(offhandSwingersMutex);
    if (offhand)
        offhandSwingers.insert(&mob);
    else
        offhandSwingers.erase(&mob);
}

// ServerboundSwingPacket(OFF_HAND).
class OffhandSwingRequestPacket : public Amethyst::CustomPacket {
public:
    std::string getName() const override
    {
        return "OffhandSwingRequestPacket";
    }

    void write(BinaryStream& out) const override
    {
    }

    Bedrock::Result<void, std::error_code> read(ReadOnlyBinaryStream& in) override
    {
        return {};
    }
};

// ClientboundAnimatePacket(SWING_OFF_HAND).
class OffhandSwingPacket : public Amethyst::CustomPacket {
public:
    uint64_t mRuntimeId = 0;

    std::string getName() const override
    {
        return "OffhandSwingPacket";
    }

    void write(BinaryStream& out) const override
    {
        out.writeUnsignedVarInt64(mRuntimeId, "runtimeId", "");
    }

    Bedrock::Result<void, std::error_code> read(ReadOnlyBinaryStream& in) override
    {
        auto runtimeId = in.getUnsignedVarInt64();
        if (!runtimeId.has_value())
            return ::nonstd::expected_lite::make_unexpected(runtimeId.error());

        mRuntimeId = *runtimeId;
        return {};
    }
};

// ServerGamePacketListenerImpl#handleAnimate. The sender already swung, so it is left out of the broadcast.
class OffhandSwingRequestPacketHandler : public Amethyst::CustomPacketHandler {
public:
    void handle(const NetworkIdentifier& networkId, NetEventCallback& callback, const Amethyst::CustomPacket&) const override
    {
        ServerNetworkHandler& serverNetwork = (ServerNetworkHandler&)callback;
        ServerPlayer* player = serverNetwork._getServerPlayer(networkId, SubClientId::PrimaryClient);
        if (player == nullptr)
            return;

        OffhandSwing::swing(*player);

        auto packet = std::make_unique<OffhandSwingPacket>();
        packet->mRuntimeId = player->getRuntimeID().rawID;
        Amethyst::GetNetworkManager().SendBroadcast(player->mPacketSender, networkId, SubClientId::PrimaryClient, std::move(packet));
    }
};

// ClientPacketListener#handleAnimate.
class OffhandSwingPacketHandler : public Amethyst::CustomPacketHandler {
public:
    void handle(const NetworkIdentifier&, NetEventCallback&, const Amethyst::CustomPacket& packet) const override
    {
        ClientInstance* client = Amethyst::GetClientCtx().mClientInstance;
        LocalPlayer* localPlayer = client != nullptr ? client->getLocalPlayer() : nullptr;
        if (localPlayer == nullptr || localPlayer->mLevel == nullptr)
            return;

        const auto& swingPacket = static_cast<const OffhandSwingPacket&>(packet);
        Actor* actor = localPlayer->mLevel->getRuntimeEntity(ActorRuntimeID(swingPacket.mRuntimeId), false);
        if (actor != nullptr && actor->isPlayer())
            OffhandSwing::swing(static_cast<Mob&>(*actor));
    }
};

// Both sides: a swing that Mob::swing starts is the main hand's.
Amethyst::InlineHook<decltype(&Mob::swing)> _Mob_swing;

bool Mob_swing(Mob* self, ActorSwingSource swingSource)
{
    const bool started = _Mob_swing(self, swingSource);
    if (started)
        setOffhandSwing(*self, false);

    return started;
}

Amethyst::InlineHook<decltype(&Player::aiStep)> _Player_aiStep;

void Player_aiStep(Player* self)
{
    _Player_aiStep(self);

    if (!self->mSwinging)
        setOffhandSwing(*self, false);
}

} // namespace

bool OffhandSwing::swing(Mob& mob)
{
    if (mob.mSwinging && mob.mSwingTime < mob.getModifiedSwingDuration() / 2 && mob.mSwingTime >= 0)
        return false;

    mob.mSwingTime = -1;
    mob.mSwinging = true;
    setOffhandSwing(mob, true);
    return true;
}

void OffhandSwing::broadcast(Player& player, const Player* except)
{
    auto packet = std::make_unique<OffhandSwingPacket>();
    packet->mRuntimeId = player.getRuntimeID().rawID;

    const UserEntityIdentifierComponent* exceptUser = except != nullptr ? except->getEntity().tryGetComponent<UserEntityIdentifierComponent>() : nullptr;
    if (exceptUser != nullptr)
        Amethyst::GetNetworkManager().SendBroadcast(player.mPacketSender, exceptUser->mNetworkId, exceptUser->mClientSubId, std::move(packet));
    else
        Amethyst::GetNetworkManager().SendBroadcast(player.mPacketSender, std::move(packet));
}

void OffhandSwing::swingLocal(LocalPlayer& player)
{
    if (swing(player))
        Amethyst::GetNetworkManager().SendToServer(player.mPacketSender, std::make_unique<OffhandSwingRequestPacket>());
}

bool OffhandSwing::isOffhandSwing(const Mob& mob)
{
    if (!mob.mSwinging)
        return false;

    std::scoped_lock lock(offhandSwingersMutex);
    return offhandSwingers.contains(&mob);
}

float OffhandSwing::interpolateAttackAnim(float attackAnim, float oAttackAnim, float a)
{
    float difference = attackAnim - oAttackAnim;
    if (difference < 0.0f)
        difference += 1.0f;

    return oAttackAnim + difference * a;
}

float OffhandSwing::getAttackAnim(const Mob& mob, float a)
{
    return isOffhandSwing(mob) ? interpolateAttackAnim(mob.mAttackAnim, mob.mOAttackAnim, a) : 0.0f;
}

void OffhandSwing::Initialize()
{
    auto& network = Amethyst::GetNetworkManager();
    network.RegisterPacketType<OffhandSwingRequestPacket>(std::make_unique<OffhandSwingRequestPacketHandler>());
    network.RegisterPacketType<OffhandSwingPacket>(std::make_unique<OffhandSwingPacketHandler>());

    auto& hooks = Amethyst::GetHookManager();
    HOOK_DIRECT_VIRTUAL(Mob, swing);
    HOOK_DIRECT_VIRTUAL(Player, aiStep);
}
