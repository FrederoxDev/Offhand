#include "features/OffhandHud.hpp"

#include <amethyst/runtime/ModContext.hpp>
#include <amethyst/runtime/utility/InlineHook.hpp>
#include <client/game/ClientInstance.hpp>
#include <client/gui/screens/controllers/HudScreenController.hpp>
#include <client/player/LocalPlayer.hpp>
#include <entity/components/ActorEquipmentComponent.hpp>
#include <world/item/registry/ItemRegistryManager.hpp>
#include <world/item/registry/ItemRegistryRef.hpp>
#include <world/level/ILevel.hpp>
#include <world/actor/player/Player.hpp>
#include <world/containers/ContainerEnum.hpp>
#include <world/containers/managers/models/HudContainerManagerModel.hpp>
#include <world/containers/models/ContainerModel.hpp>
#include <world/containers/models/OffhandContainerModel.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <string>

namespace {

static_assert(sizeof(OffhandContainerModel) == sizeof(ContainerModel) + sizeof(Player*));

// OffhandContainerModel::OffhandContainerModel is inlined. The deleter frees into this module's allocator, since this
// module allocated it.
std::shared_ptr<ContainerModel> createOffhandContainerModel(Player& player)
{
    void* storage = ::operator new(sizeof(OffhandContainerModel));
    auto* model = static_cast<ContainerModel*>(storage);

    const FullContainerName name{ContainerEnumName::OffhandContainer, std::nullopt};
    ContainerModel::$constructor(model, name, 1, ContainerCategory::PlayerInventory, player.isClientSide());

    *static_cast<uintptr_t*>(storage) = OffhandContainerModel::$vtable_for_OffhandContainerModel;
    *reinterpret_cast<Player**>(static_cast<std::byte*>(storage) + sizeof(ContainerModel)) = &player;

    return std::shared_ptr<ContainerModel>(model, [](ContainerModel* containerModel) {
        containerModel->~ContainerModel();
        ::operator delete(containerModel);
    });
}

Amethyst::InlineHook<decltype(&HudContainerManagerModel::_postInit)> _HudContainerManagerModel__postInit;

ContainerScreenContext HudContainerManagerModel__postInit(HudContainerManagerModel* self)
{
    ContainerScreenContext context = _HudContainerManagerModel__postInit(self);
    self->_addContainer(createOffhandContainerModel(self->mPlayer));
    return context;
}

// Player::~Player releases the HUD's container models after the entity has lost its components, which
// OffhandContainerModel::releaseResources doesn't expect since screens release theirs while the player still exists.
// With the hand container gone its listeners are too, so only ContainerModel's own part is left to release.
Amethyst::InlineHook<decltype(&OffhandContainerModel::releaseResources)> _OffhandContainerModel_releaseResources;

void OffhandContainerModel_releaseResources(OffhandContainerModel* self)
{
    if (self->mPlayer.getEntity().tryGetComponent<ActorEquipmentComponent>() != nullptr) {
        _OffhandContainerModel_releaseResources(self);
        return;
    }

    ContainerModel::$direct_releaseResources(self);
}

// A GUI scale change rebinds every screen outside MinecraftGame::_update's ItemRegistryManager::ScopedItemRegistry.
// The offhand slot is always visible, so its item bindings run then and ItemStackBase::getColor would build an
// ItemDescriptor with no item registry.
using HudCollectionBind = bool (HudScreenController::*)(const std::string&, uint32_t, int, const std::string&, uint32_t, const std::string&, UIPropertyBag&);
Amethyst::InlineHook<HudCollectionBind> _HudScreenController_bindCollection;

bool HudScreenController_bindCollection(HudScreenController* self, const std::string& collectionName, uint32_t collectionNameHash, int collectionIndex, const std::string& bindingName, uint32_t bindingNameHash, const std::string& bindingNameOverride, UIPropertyBag& bag)
{
    ClientInstance* client = Amethyst::GetClientCtx().mClientInstance;
    LocalPlayer* player = client != nullptr ? client->getLocalPlayer() : nullptr;
    if (!ItemRegistryManager::getItemRegistry().mWeakRegistry.expired() || player == nullptr || player->mLevel == nullptr)
        return _HudScreenController_bindCollection(self, collectionName, collectionNameHash, collectionIndex, bindingName, bindingNameHash, bindingNameOverride, bag);

    ItemRegistryManager::ScopedItemRegistry registry(player->mLevel->getItemRegistry().mWeakRegistry);
    return _HudScreenController_bindCollection(self, collectionName, collectionNameHash, collectionIndex, bindingName, bindingNameHash, bindingNameOverride, bag);
}

} // namespace

void OffhandHud::Initialize()
{
    auto& hooks = Amethyst::GetHookManager();
    HOOK_DIRECT_VIRTUAL(HudContainerManagerModel, _postInit);
    HOOK_DIRECT_VIRTUAL(OffhandContainerModel, releaseResources);
    HOOK_DIRECT_VIRTUAL_NAMED(HudScreenController, bind, HudScreenController_bindCollection);
}
