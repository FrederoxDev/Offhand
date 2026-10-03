#pragma once

class LocalPlayer;
class Mob;
class Player;

namespace OffhandSwing {

// LivingEntity#swing(OFF_HAND). False when the current swing is not far enough along to restart.
bool swing(Mob& mob);

// LocalPlayer#swing(OFF_HAND). The server relays it to the other players.
void swingLocal(LocalPlayer& player);

// Server: ServerPlayer#swing(OFF_HAND, true)'s broadcast. Includes the player itself unless it is `except`.
void broadcast(Player& player, const Player* except);

// LivingEntity#swingingArm == OFF_HAND.
bool isOffhandSwing(const Mob& mob);

// Mob::getAttackAnim is inlined.
float interpolateAttackAnim(float attackAnim, float oAttackAnim, float a);

// LivingEntity#getAttackAnim for the offhand arm: 0 unless the offhand is the arm swinging.
float getAttackAnim(const Mob& mob, float a);

void Initialize();

} // namespace OffhandSwing
