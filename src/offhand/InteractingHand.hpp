#pragma once

class Player;

namespace Offhand {

// Java's Entity#interact(player, hand) checks one hand's item at a time. Bedrock's interaction filters check both
// hands (Player::getAllHandEquipment), so while this scope is active they see only the selected slot: the main hand,
// or the offhand inside a HandSwapScope.
class InteractingHandScope {
public:
    explicit InteractingHandScope(const Player& player);
    ~InteractingHandScope();

    InteractingHandScope(const InteractingHandScope&) = delete;
    InteractingHandScope& operator=(const InteractingHandScope&) = delete;

    static bool isInteracting(const Player& player);

private:
    const Player* mOuter;
};

} // namespace Offhand
