#include "features/SwapOffhand.hpp"

#include "offhand/Hands.hpp"

#include <amethyst/runtime/ModContext.hpp>
#include <amethyst/runtime/networking/NetworkManager.hpp>
#include <amethyst/runtime/utility/InlineHook.hpp>
#include <client/game/ClientInstance.hpp>
#include <client/gui/UITypes.hpp>
#include <client/player/LocalPlayer.hpp>
#include <Input/HIDController.hpp>
#include <SubClientId.hpp>
#include <network/LoopbackPacketSender.hpp>
#include <network/ServerNetworkHandler.hpp>
#include <server/ServerPlayer.hpp>
#include <world/ContainerID.hpp>
#include <world/actor/player/Inventory.hpp>
#include <world/actor/player/PlayerInventory.hpp>
#include <world/item/ItemStack.hpp>
#include <world/level/Level.hpp>

#include <spyglass/input/Builder.hpp>
#include <spyglass/input/Dispatcher.hpp>

#include <mutex>
#include <vector>

namespace {

using namespace Spyglass::Input;

constexpr const char* SWAP_ACTION_ID = "offhand.swap";
constexpr int KEY_F = 'F';
constexpr float CLIENT_TICK_SECONDS = 0.05f;

class SwapOffhandPacket : public Amethyst::CustomPacket {
public:
    std::string getName() const override
    {
        return "SwapOffhandPacket";
    }

    void write(BinaryStream& out) const override
    {
    }

    Bedrock::Result<void, std::error_code> read(ReadOnlyBinaryStream& in) override
    {
        return {};
    }
};

// ServerGamePacketListenerImpl#handlePlayerAction, SWAP_ITEM_WITH_OFFHAND.
class SwapOffhandPacketHandler : public Amethyst::CustomPacketHandler {
public:
    void handle(const NetworkIdentifier& networkId, NetEventCallback& callback, const Amethyst::CustomPacket& packet) const override
    {
        ServerNetworkHandler& serverNetwork = (ServerNetworkHandler&)callback;
        ServerPlayer* player = serverNetwork._getServerPlayer(networkId, SubClientId::PrimaryClient);
        if (player == nullptr || player->isSpectator())
            return;

        PlayerInventory& supplies = player->getSupplies();
        if (supplies.mSelectedContainerId != ContainerID::CONTAINER_ID_INVENTORY)
            return;

        const ItemStack offhandItem = Offhand::getItem(*player);
        const ItemStack mainhandItem = supplies.getSelectedItem();

        player->setOffhandSlot(mainhandItem);
        supplies.mInventory->setItem(supplies.mSelected, offhandItem);
        player->stopUsingItem();
        player->sendInventory(false);
    }
};

Registry registry;
Keymap keymap;
ContextStack contextStack;
Dispatcher dispatcher{registry, keymap, contextStack};

std::mutex pendingInputMutex;
std::vector<InputEvent> pendingInput;

void feedKey(int keyCode, bool down)
{
    InputEvent event;
    event.device = {DeviceKind::KeyboardMouse, 0};
    event.control = keyCode;
    event.value = down ? 1.0f : 0.0f;

    std::scoped_lock lock(pendingInputMutex);
    pendingInput.push_back(event);
}

// Minecraft#handleKeybinds, keySwapOffhand.
void onSwapPressed()
{
    ClientInstance* client = Amethyst::GetClientCtx().mClientInstance;
    if (client == nullptr || client->getTopSceneType() != ui::SceneType::HUD_SCENE)
        return;

    LocalPlayer* player = client->getLocalPlayer();
    if (player == nullptr || player->isSpectator())
        return;

    Amethyst::GetNetworkManager().SendToServer(*client->mPacketSender, std::make_unique<SwapOffhandPacket>());
}

using HIDController_onKeyDown_t = void (HIDController::*)(int, ::Bedrock::Input::KeyboardEventProcessor::InputOrigin);
Amethyst::InlineHook<HIDController_onKeyDown_t> _HIDController_onKeyDown;
Amethyst::InlineHook<decltype(&HIDController::onKeyUp)> _HIDController_onKeyUp;

void HIDController_onKeyDown(HIDController* self, int keyCode, ::Bedrock::Input::KeyboardEventProcessor::InputOrigin origin)
{
    feedKey(keyCode, true);
    _HIDController_onKeyDown(self, keyCode, origin);
}

void HIDController_onKeyUp(HIDController* self, int keyCode)
{
    feedKey(keyCode, false);
    _HIDController_onKeyUp(self, keyCode);
}

Amethyst::InlineHook<decltype(&Level::tick)> _Level_tick;

void Level_tick(Level* self)
{
    _Level_tick(self);

    if (!self->isClientSide())
        return;

    std::vector<InputEvent> events;
    {
        std::scoped_lock lock(pendingInputMutex);
        events.swap(pendingInput);
    }

    for (const InputEvent& event : events)
        dispatcher.feed(event);

    dispatcher.tick(CLIENT_TICK_SECONDS);
}

} // namespace

void SwapOffhand::Initialize()
{
    registry.add(make(SWAP_ACTION_ID)
        .label("Swap Item With Offhand")
        .binding(chord(KEY_F), press())
        .handler([](const ActionFireInfo&) { onSwapPressed(); }));

    Amethyst::GetNetworkManager().RegisterPacketType<SwapOffhandPacket>(std::make_unique<SwapOffhandPacketHandler>());

    auto& hooks = Amethyst::GetHookManager();
    HOOK_DIRECT_VIRTUAL(HIDController, onKeyDown);
    HOOK_DIRECT_VIRTUAL(HIDController, onKeyUp);
    HOOK_DIRECT_VIRTUAL(Level, tick);
}
