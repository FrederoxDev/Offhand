#include "features/OffhandHud.hpp"
#include "features/OffhandInput.hpp"
#include "features/OffhandItemUse.hpp"
#include "features/OffhandItems.hpp"
#include "features/OffhandMobs.hpp"
#include "features/OffhandPlayerModel.hpp"
#include "features/OffhandRendering.hpp"
#include "features/OffhandSwing.hpp"
#include "features/OffhandSync.hpp"
#include "features/OffhandTransactions.hpp"
#include "features/OffhandUseSync.hpp"
#include "features/ShieldBlocking.hpp"
#include "features/SwapOffhand.hpp"

#include <amethyst/runtime/ModContext.hpp>
#include <amethyst/runtime/resource/PackManager.hpp>

#define ModFunction extern "C" __declspec(dllexport)

ModFunction void Initialize(AmethystContext& ctx, const Amethyst::Mod& mod)
{
    Amethyst::InitializeAmethystMod(ctx, mod);
    ctx.mPackManager->RegisterNewPack(&mod, "main_rp", PackType::Resources);

    OffhandItems::Initialize();
    OffhandSync::Initialize();
    OffhandInput::Initialize();
    OffhandTransactions::Initialize();
    OffhandItemUse::Initialize();
    OffhandSwing::Initialize();
    OffhandUseSync::Initialize();
    OffhandPlayerModel::Initialize();
    OffhandMobs::Initialize();
    OffhandRendering::Initialize();
    OffhandHud::Initialize();
    SwapOffhand::Initialize();
    ShieldBlocking::Initialize();
}
