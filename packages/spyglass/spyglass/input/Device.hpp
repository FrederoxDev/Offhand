#pragma once
#include <string>

namespace Spyglass::Input {

enum class DeviceKind {
	KeyboardMouse,
	Gamepad,
	Touch,
	Custom,
};

struct DeviceId {
	DeviceKind kind  = DeviceKind::KeyboardMouse;
	int        index = 0;

	bool operator==(const DeviceId& o) const = default;
};

/**
Control id meaning is per-device. KeyboardMouse uses Win32 VK_* plus the
SyntheticControl::* range below for things VK_ doesn't cover. Gamepad
uses the GamepadButton / GamepadAxis ids — hosts on weird platforms
translate to these before feeding events.
*/
namespace SyntheticControl {
	constexpr int WheelUp     = 0x10000;
	constexpr int WheelDown   = 0x10001;
	constexpr int MouseDeltaX = 0x10002;
	constexpr int MouseDeltaY = 0x10003;
}

namespace GamepadButton {
	constexpr int A          = 0x20000;
	constexpr int B          = 0x20001;
	constexpr int X          = 0x20002;
	constexpr int Y          = 0x20003;
	constexpr int LB         = 0x20004;
	constexpr int RB         = 0x20005;
	constexpr int Back       = 0x20006;
	constexpr int Start      = 0x20007;
	constexpr int LStick     = 0x20008;
	constexpr int RStick     = 0x20009;
	constexpr int DPadUp     = 0x2000A;
	constexpr int DPadDown   = 0x2000B;
	constexpr int DPadLeft   = 0x2000C;
	constexpr int DPadRight  = 0x2000D;
}

namespace GamepadAxis {
	constexpr int LStickX    = 0x21000;
	constexpr int LStickY    = 0x21001;
	constexpr int RStickX    = 0x21002;
	constexpr int RStickY    = 0x21003;
	constexpr int LTrigger   = 0x21004;
	constexpr int RTrigger   = 0x21005;
}

// Buttons use 0.0 / 1.0, axes use their native range (typically -1..1).
struct InputEvent {
	DeviceId device;
	int      control   = 0;
	float    value     = 0.0f;
	int      modifiers = 0;  // KB modifier mask at event time, KeyboardMouse only
};

}
