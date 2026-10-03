#include "features/OffhandItems.hpp"

#include <amethyst/runtime/ModContext.hpp>
#include <amethyst/runtime/utility/InlineHook.hpp>
#include <Core/String/StringHash.hpp>
#include <SharedTypes/legacy/item/UseAnimation.hpp>
#include <world/actor/Actor.hpp>
#include <world/item/Item.hpp>
#include <world/item/ItemStack.hpp>
#include <world/item/registry/ItemRegistry.hpp>

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace {

// Java's ShieldItem#getUseDuration; Bedrock's shield is never used, so it has none.
constexpr int SHIELD_USE_DURATION = 72000;

// Java allows any item in the offhand, so Item::mAllowOffhand is set on every item. The vanilla value is kept for the
// flag's other uses. Locked: item registries finish on both sides and the server reads this while equipping mobs.
std::shared_mutex vanillaAllowOffhandMutex;
std::unordered_map<uint64_t, bool> vanillaAllowOffhand;

bool allowsOffhandInVanilla(const ItemStack& stack)
{
    const Item* item = stack.getItem();
    if (item == nullptr)
        return false;

    std::shared_lock lock(vanillaAllowOffhandMutex);
    const auto found = vanillaAllowOffhand.find(item->mFullName.getHash());
    return found != vanillaAllowOffhand.end() ? found->second : item->mAllowOffhand;
}

Amethyst::InlineHook<decltype(&ItemRegistry::finishedRegistration)> _ItemRegistry_finishedRegistration;

void ItemRegistry_finishedRegistration(ItemRegistry* self)
{
    _ItemRegistry_finishedRegistration(self);

    std::unique_lock lock(vanillaAllowOffhandMutex);
    for (const SharedPtr<Item>& item : self->mItemRegistry) {
        if (!item)
            continue;

        // A registry finishing again would otherwise record the flag this sets.
        vanillaAllowOffhand.try_emplace(item->mFullName.getHash(), static_cast<bool>(item->mAllowOffhand));
        item->mAllowOffhand = true;

        if (item->mUseAnim == SharedTypes::Legacy::UseAnimation::Block && item->mMaxUseDuration == 0)
            item->mMaxUseDuration = SHIELD_USE_DURATION;
    }
}

// Server only.
thread_local const Actor* equippingActor = nullptr;

Amethyst::InlineHook<void (Actor::*)(const std::string&)> _Actor_equip;

void Actor_equip(Actor* self, const std::string& lootTableFilePath)
{
    const Actor* outer = equippingActor;
    equippingActor = self;
    _Actor_equip(self, lootTableFilePath);
    equippingActor = outer;
}

// Actor::equip puts every item allowed in the offhand there and carries the rest, so with every item allowed a
// skeleton would spawn holding its bow in the offhand. Loot table equipment goes where vanilla puts it.
Amethyst::InlineHook<decltype(&Actor::setOffhandSlot)> _Actor_setOffhandSlot;

void Actor_setOffhandSlot(Actor* self, const ItemStack& item)
{
    if (equippingActor == self && !allowsOffhandInVanilla(item)) {
        self->setCarriedItem(item);
        return;
    }

    _Actor_setOffhandSlot(self, item);
}

} // namespace

void OffhandItems::Initialize()
{
    auto& hooks = Amethyst::GetHookManager();
    HOOK(ItemRegistry, finishedRegistration);
    HOOK(Actor, equip);
    HOOK_DIRECT_VIRTUAL(Actor, setOffhandSlot);
}
