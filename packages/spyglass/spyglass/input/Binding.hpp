#pragma once
#include "Device.hpp"
#include "Processor.hpp"
#include "Types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Spyglass::Input {

class InputState;

/**
References one device control plus required modifier bits. `modifiers`
is matched exactly — plain Space (modifiers = 0) does not match
Ctrl+Space (modifiers = ModCtrl) and vice versa.
*/
struct ControlRef {
	DeviceId device;
	int      control   = 0;
	int      modifiers = 0;
};

/**
Anything that produces a value from raw device state. Concrete subclasses
cover single controls, axis compositions, and multi-step chord sequences.
Subclasses return nullopt from evaluate() when they have no signal this
frame; non-null means "fire with this value if the trigger agrees".
*/
class Binding {
public:
	virtual ~Binding() = default;

	virtual ValueType valueType() const = 0;
	virtual std::optional<ActionValue> evaluate(const InputState& state) const = 0;

	// Reverse lookup for the rebind UI and conflict checking.
	virtual std::vector<ControlRef> referencedControls() const = 0;

	using KeyNameFn = std::string (*)(int control);
	virtual std::string toString(KeyNameFn nameFn = nullptr) const = 0;
};

class ChordBinding : public Binding {
public:
	ControlRef chord;

	ValueType valueType() const override { return ValueType::Bool; }
	std::optional<ActionValue> evaluate(const InputState&) const override;
	std::vector<ControlRef>    referencedControls() const override;
	std::string                toString(KeyNameFn) const override;
};

/**
Multi-step chord sequence (e.g. Ctrl+K Ctrl+O). Fires once when the full
sequence completes. The Dispatcher's sequence buffer coordinates
partial-match state — per-frame evaluate() returns nullopt because the
binding has no local notion of progress.
*/
class ChordSequenceBinding : public Binding {
public:
	std::vector<ControlRef> steps;
	float                   stepTimeoutSeconds = 1.0f;

	ValueType valueType() const override { return ValueType::Bool; }
	std::optional<ActionValue> evaluate(const InputState&) const override;
	std::vector<ControlRef>    referencedControls() const override;
	std::string                toString(KeyNameFn) const override;
};

class AxisBinding : public Binding {
public:
	ControlRef                              control;
	std::vector<std::unique_ptr<Processor>> processors;

	ValueType valueType() const override { return ValueType::Axis1D; }
	std::optional<ActionValue> evaluate(const InputState&) const override;
	std::vector<ControlRef>    referencedControls() const override;
	std::string                toString(KeyNameFn) const override;
};

// 2D from two 1D bindings (mouse delta X+Y, stick X+Y).
class Composite2DBinding : public Binding {
public:
	std::unique_ptr<Binding>                x;
	std::unique_ptr<Binding>                y;
	std::vector<std::unique_ptr<Processor>> processors;

	ValueType valueType() const override { return ValueType::Axis2D; }
	std::optional<ActionValue> evaluate(const InputState&) const override;
	std::vector<ControlRef>    referencedControls() const override;
	std::string                toString(KeyNameFn) const override;
};

// WASD-style 2D from four button bindings.
class DirectionalCompositeBinding : public Binding {
public:
	std::unique_ptr<Binding> up;
	std::unique_ptr<Binding> down;
	std::unique_ptr<Binding> left;
	std::unique_ptr<Binding> right;
	std::vector<std::unique_ptr<Processor>> processors;

	ValueType valueType() const override { return ValueType::Axis2D; }
	std::optional<ActionValue> evaluate(const InputState&) const override;
	std::vector<ControlRef>    referencedControls() const override;
	std::string                toString(KeyNameFn) const override;
};

}
