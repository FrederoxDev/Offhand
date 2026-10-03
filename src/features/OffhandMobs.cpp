#include "features/OffhandMobs.hpp"

#include "features/OffhandSwing.hpp"
#include "offhand/HandSwapScope.hpp"
#include "offhand/Hands.hpp"
#include "offhand/InteractingHand.hpp"

#include <amethyst/runtime/ModContext.hpp>
#include <amethyst/runtime/utility/InlineHook.hpp>
#include <Core/Math/Vec3.hpp>
#include <network/Packet.hpp>
#include <network/packet/AnimatePacket.hpp>
#include <world/actor/Mob.hpp>
#include <world/actor/ai/goal/TemptBaseGoal.hpp>
#include <world/actor/ai/goal/TemptGoal.hpp>
#include <world/actor/player/Player.hpp>
#include <world/actor/player/PlayerInventory.hpp>
#include <world/item/Item.hpp>
#include <world/item/ItemDescriptor.hpp>
#include <world/item/ItemStack.hpp>
#include <world/level/dimension/Dimension.hpp>

#include <memory>
#include <vector>

namespace {

// Both sides: while a player interacts with an entity, its interaction filters see only the hand interacting.
Amethyst::InlineHook<decltype(&Player::getAllHandEquipment)> _Player_getAllHandEquipment;

std::vector<const ItemStack*> Player_getAllHandEquipment(const Player* self)
{
    if (!Offhand::InteractingHandScope::isInteracting(*self))
        return _Player_getAllHandEquipment(self);

    std::vector<const ItemStack*> hand;
    const ItemStack& selected = const_cast<Player*>(self)->getSupplies().getSelectedItem();
    if (Offhand::hasItem(selected))
        hand.push_back(&selected);
    return hand;
}

bool tempts(const TemptBaseGoal& goal, const ItemStack& item)
{
    if (!Offhand::hasItem(item))
        return false;

    for (const ItemDescriptor& descriptor : goal.mItems) {
        if (descriptor.sameItem(item, false))
            return true;
    }
    return false;
}

float distanceSquared(const Vec3& a, const Vec3& b)
{
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return x * x + y * y + z * z;
}

// Server: Java's TemptGoal#isTemptedBy tempts with either hand; TemptBaseGoal only looks at the selected slot.
template <typename Evaluate>
bool temptedByEitherHand(TemptBaseGoal& goal, Evaluate&& evaluate)
{
    if (evaluate())
        return true;

    Mob& mob = goal.mMob;
    const Vec3& mobPosition = mob.getPosition();
    const float range = goal.mTemptDistance + 1.0f;

    std::vector<Player*> candidates;
    mob.getDimension().forEachPlayer([&](Player& player) {
        if (&player != static_cast<Actor*>(&mob)
            && distanceSquared(player.getPosition(), mobPosition) <= range * range
            && tempts(goal, Offhand::getItem(player))
            && !tempts(goal, player.getSupplies().getSelectedItem())) {
            candidates.push_back(&player);
        }
        return true;
    });

    if (candidates.empty())
        return false;

    std::vector<std::unique_ptr<Offhand::HandSwapScope>> scopes;
    scopes.reserve(candidates.size());
    for (Player* player : candidates)
        scopes.push_back(std::make_unique<Offhand::HandSwapScope>(*player));

    const bool tempted = evaluate();

    // Scopes nest, so they end in the reverse order they began.
    while (!scopes.empty())
        scopes.pop_back();

    return tempted;
}

Amethyst::InlineHook<decltype(&TemptGoal::canUse)> _TemptGoal_canUse;

bool TemptGoal_canUse(TemptGoal* self)
{
    return temptedByEitherHand(*self, [self] { return _TemptGoal_canUse(self); });
}

Amethyst::InlineHook<decltype(&TemptBaseGoal::canContinueToUse)> _TemptBaseGoal_canContinueToUse;

bool TemptBaseGoal_canContinueToUse(TemptBaseGoal* self)
{
    return temptedByEitherHand(*self, [self] { return _TemptBaseGoal_canContinueToUse(self); });
}

// Both sides: InteractComponent ("swing": true), DiggerItem and SwingEventResponse broadcast a main hand swing, which
// inside a HandSwapScope belongs to the offhand. The client drops it; its LocalPlayer::swing already asked the server.
Amethyst::InlineHook<decltype(&Dimension::sendPacketForEntity)> _Dimension_sendPacketForEntity;

void Dimension_sendPacketForEntity(Dimension* self, const Actor& actor, const Packet& packet, const Player* except)
{
    if (packet.getId() == MinecraftPacketIds::Animate && actor.isPlayer()) {
        Player& player = const_cast<Player&>(static_cast<const Player&>(actor));
        const auto& animate = static_cast<const AnimatePacket&>(packet);
        if (animate.mPayload.mAction == AnimatePacketPayload::Action::Swing && Offhand::HandSwapScope::isActive(player)) {
            if (!player.isClientSide())
                OffhandSwing::broadcast(player, except);
            return;
        }
    }

    _Dimension_sendPacketForEntity(self, actor, packet, except);
}

} // namespace

void OffhandMobs::Initialize()
{
    auto& hooks = Amethyst::GetHookManager();
    HOOK_DIRECT_VIRTUAL(Player, getAllHandEquipment);
    HOOK_DIRECT_VIRTUAL(TemptGoal, canUse);
    HOOK_DIRECT_VIRTUAL(TemptBaseGoal, canContinueToUse);
    HOOK_DIRECT_VIRTUAL(Dimension, sendPacketForEntity);
}
