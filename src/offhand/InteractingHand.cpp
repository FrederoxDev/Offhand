#include "offhand/InteractingHand.hpp"

namespace {

thread_local const Player* interactingPlayer = nullptr;

} // namespace

Offhand::InteractingHandScope::InteractingHandScope(const Player& player)
    : mOuter(interactingPlayer)
{
    interactingPlayer = &player;
}

Offhand::InteractingHandScope::~InteractingHandScope()
{
    interactingPlayer = mOuter;
}

bool Offhand::InteractingHandScope::isInteracting(const Player& player)
{
    return interactingPlayer == &player;
}
