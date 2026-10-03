#pragma once
#include <cstdint>
#include <variant>

namespace Spyglass::Input {

enum Modifiers : int {
	ModNone  = 0,
	ModCtrl  = 1 << 0,
	ModShift = 1 << 1,
	ModAlt   = 1 << 2,
};

enum class ValueType {
	Bool,
	Axis1D,
	Axis2D,
	Axis3D,
};

/**
Lifecycle phase reported to an action handler. Triggers decide which
phases fire and when. `Started` and `Completed` bracket a session;
`Triggered` is the "do the thing" phase. `Canceled` means the trigger
ended without firing (release before hold threshold, sequence timeout).
*/
enum class ActionPhase {
	Started,
	Triggered,
	Completed,
	Canceled,
};

struct Vec2 { float x = 0.0f, y = 0.0f; };
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

// variant index matches ValueType: 0=Bool, 1=Axis1D, 2=Axis2D, 3=Axis3D.
using ActionValue = std::variant<bool, float, Vec2, Vec3>;

struct ActionFireInfo {
	ActionPhase phase   = ActionPhase::Triggered;
	ActionValue value   = false;
	float       elapsed = 0.0f;  // seconds since Started fired
};

}
