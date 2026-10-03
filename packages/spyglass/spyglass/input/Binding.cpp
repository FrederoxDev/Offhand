#include "Binding.hpp"
#include "Dispatcher.hpp"

#include <cmath>
#include <sstream>

namespace Spyglass::Input {

namespace {

std::string defaultControlName(int control)
{
	if (control >= 'A' && control <= 'Z') return std::string(1, static_cast<char>(control));
	if (control >= '0' && control <= '9') return std::string(1, static_cast<char>(control));
	if (control >= 0x70 && control <= 0x7B) {
		std::string s = "F";
		s += std::to_string(control - 0x70 + 1);
		return s;
	}
	switch (control) {
		case 0x01: return "LMB";
		case 0x02: return "RMB";
		case 0x04: return "MMB";
		case 0x08: return "Backspace";
		case 0x09: return "Tab";
		case 0x0D: return "Enter";
		case 0x10: return "Shift";
		case 0x11: return "Ctrl";
		case 0x12: return "Alt";
		case 0x1B: return "Esc";
		case 0x20: return "Space";
		case 0x21: return "PageUp";
		case 0x22: return "PageDown";
		case 0x23: return "End";
		case 0x24: return "Home";
		case 0x25: return "Left";
		case 0x26: return "Up";
		case 0x27: return "Right";
		case 0x28: return "Down";
		case 0x2D: return "Insert";
		case 0x2E: return "Delete";
		case SyntheticControl::WheelUp:     return "WheelUp";
		case SyntheticControl::WheelDown:   return "WheelDown";
		case SyntheticControl::MouseDeltaX: return "MouseX";
		case SyntheticControl::MouseDeltaY: return "MouseY";
		default: return "Control" + std::to_string(control);
	}
}

std::string formatChord(const ControlRef& ref, Binding::KeyNameFn nameFn)
{
	std::string s;
	if (ref.modifiers & ModCtrl)  s += "Ctrl+";
	if (ref.modifiers & ModShift) s += "Shift+";
	if (ref.modifiers & ModAlt)   s += "Alt+";
	s += nameFn ? nameFn(ref.control) : defaultControlName(ref.control);
	return s;
}

bool chordPressed(const InputState& state, const ControlRef& ref)
{
	if (ref.device.kind == DeviceKind::KeyboardMouse) {
		if (state.keyboardModifiers() != ref.modifiers) return false;
	}
	return state.isHeld(ref.device, ref.control);
}

float applyProcessors1D(const std::vector<std::unique_ptr<Processor>>& ps, float v)
{
	for (const auto& p : ps) v = p->process1D(v);
	return v;
}

Vec2 applyProcessors2D(const std::vector<std::unique_ptr<Processor>>& ps, Vec2 v)
{
	for (const auto& p : ps) v = p->process2D(v);
	return v;
}

}

// --- ChordBinding ---

std::optional<ActionValue> ChordBinding::evaluate(const InputState& state) const
{
	if (!chordPressed(state, chord)) return std::nullopt;
	return ActionValue{true};
}

std::vector<ControlRef> ChordBinding::referencedControls() const
{
	return {chord};
}

std::string ChordBinding::toString(KeyNameFn nameFn) const
{
	return formatChord(chord, nameFn);
}

// --- ChordSequenceBinding ---

std::optional<ActionValue> ChordSequenceBinding::evaluate(const InputState&) const
{
	// Multi-step sequences are tracked centrally by Dispatcher (see its
	// SequenceProgress bookkeeping); per-frame evaluate isn't meaningful.
	return std::nullopt;
}

std::vector<ControlRef> ChordSequenceBinding::referencedControls() const
{
	return steps;
}

std::string ChordSequenceBinding::toString(KeyNameFn nameFn) const
{
	std::string s;
	for (size_t i = 0; i < steps.size(); ++i) {
		if (i) s += ' ';
		s += formatChord(steps[i], nameFn);
	}
	return s;
}

// --- AxisBinding ---

std::optional<ActionValue> AxisBinding::evaluate(const InputState& state) const
{
	float raw = state.value(control.device, control.control);
	raw = applyProcessors1D(processors, raw);
	if (raw == 0.0f) return std::nullopt;
	return ActionValue{raw};
}

std::vector<ControlRef> AxisBinding::referencedControls() const
{
	return {control};
}

std::string AxisBinding::toString(KeyNameFn nameFn) const
{
	return formatChord(control, nameFn);
}

// --- Composite2DBinding ---

std::optional<ActionValue> Composite2DBinding::evaluate(const InputState& state) const
{
	Vec2 v{};
	if (x) {
		if (auto vx = x->evaluate(state); vx && std::holds_alternative<float>(*vx)) {
			v.x = std::get<float>(*vx);
		}
	}
	if (y) {
		if (auto vy = y->evaluate(state); vy && std::holds_alternative<float>(*vy)) {
			v.y = std::get<float>(*vy);
		}
	}
	v = applyProcessors2D(processors, v);
	if (v.x == 0.0f && v.y == 0.0f) return std::nullopt;
	return ActionValue{v};
}

std::vector<ControlRef> Composite2DBinding::referencedControls() const
{
	std::vector<ControlRef> out;
	if (x) for (auto& r : x->referencedControls()) out.push_back(r);
	if (y) for (auto& r : y->referencedControls()) out.push_back(r);
	return out;
}

std::string Composite2DBinding::toString(KeyNameFn nameFn) const
{
	std::string s = "(";
	s += x ? x->toString(nameFn) : "_";
	s += ",";
	s += y ? y->toString(nameFn) : "_";
	s += ")";
	return s;
}

// --- DirectionalCompositeBinding ---

namespace {
float buttonValue(const Binding* b, const InputState& state)
{
	if (!b) return 0.0f;
	auto v = b->evaluate(state);
	if (!v) return 0.0f;
	if (std::holds_alternative<bool>(*v))  return std::get<bool>(*v) ? 1.0f : 0.0f;
	if (std::holds_alternative<float>(*v)) return std::get<float>(*v);
	return 0.0f;
}
}

std::optional<ActionValue> DirectionalCompositeBinding::evaluate(const InputState& state) const
{
	Vec2 v{};
	v.x = buttonValue(right.get(), state) - buttonValue(left.get(), state);
	v.y = buttonValue(up.get(),    state) - buttonValue(down.get(), state);
	v = applyProcessors2D(processors, v);
	if (v.x == 0.0f && v.y == 0.0f) return std::nullopt;
	return ActionValue{v};
}

std::vector<ControlRef> DirectionalCompositeBinding::referencedControls() const
{
	std::vector<ControlRef> out;
	auto add = [&](const Binding* b) {
		if (!b) return;
		for (auto& r : b->referencedControls()) out.push_back(r);
	};
	add(up.get()); add(down.get()); add(left.get()); add(right.get());
	return out;
}

std::string DirectionalCompositeBinding::toString(KeyNameFn nameFn) const
{
	std::string s = "[";
	s += up    ? up->toString(nameFn)    : "_";  s += "/";
	s += down  ? down->toString(nameFn)  : "_";  s += "/";
	s += left  ? left->toString(nameFn)  : "_";  s += "/";
	s += right ? right->toString(nameFn) : "_";  s += "]";
	return s;
}

}
