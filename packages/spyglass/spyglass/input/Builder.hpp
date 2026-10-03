#pragma once
#include "Action.hpp"
#include "Binding.hpp"
#include "Device.hpp"
#include "Trigger.hpp"
#include "Types.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
Fluent builder and free helpers for declaring Actions. Binding/Trigger
factories return owning unique_ptrs so they can be composed into
composites and sequences.

	registry.add(
		make("playback.toggle")
			.label("Play / Pause")
			.context("editor")
			.binding(chord(VK_SPACE), press())
			.handler([](const ActionFireInfo&) { ... })
	);
*/
namespace Spyglass::Input {

inline ControlRef kb(int control, int modifiers = 0)
{
	return ControlRef{{DeviceKind::KeyboardMouse, 0}, control, modifiers};
}

inline ControlRef pad(int control, int padIndex = 0)
{
	return ControlRef{{DeviceKind::Gamepad, padIndex}, control, 0};
}

inline std::unique_ptr<ChordBinding> chord(ControlRef ref)
{
	auto b = std::make_unique<ChordBinding>();
	b->chord = ref;
	return b;
}

inline std::unique_ptr<ChordBinding> chord(int control, int modifiers = 0)
{
	return chord(kb(control, modifiers));
}

inline std::unique_ptr<AxisBinding> axis(ControlRef ref, std::vector<std::unique_ptr<Processor>> processors = {})
{
	auto b = std::make_unique<AxisBinding>();
	b->control    = ref;
	b->processors = std::move(processors);
	return b;
}

inline std::unique_ptr<ChordSequenceBinding> sequence(std::vector<ControlRef> steps, float stepTimeoutSeconds = 1.0f)
{
	auto b = std::make_unique<ChordSequenceBinding>();
	b->steps              = std::move(steps);
	b->stepTimeoutSeconds = stepTimeoutSeconds;
	return b;
}

inline std::unique_ptr<Composite2DBinding> composite2D(
	std::unique_ptr<Binding> x,
	std::unique_ptr<Binding> y,
	std::vector<std::unique_ptr<Processor>> processors = {})
{
	auto b = std::make_unique<Composite2DBinding>();
	b->x          = std::move(x);
	b->y          = std::move(y);
	b->processors = std::move(processors);
	return b;
}

inline std::unique_ptr<DirectionalCompositeBinding> directional(
	std::unique_ptr<Binding> up,
	std::unique_ptr<Binding> down,
	std::unique_ptr<Binding> left,
	std::unique_ptr<Binding> right,
	std::vector<std::unique_ptr<Processor>> processors = {})
{
	auto b = std::make_unique<DirectionalCompositeBinding>();
	b->up         = std::move(up);
	b->down       = std::move(down);
	b->left       = std::move(left);
	b->right      = std::move(right);
	b->processors = std::move(processors);
	return b;
}

inline std::unique_ptr<Trigger> press()                       { return std::make_unique<PressTrigger>(); }
inline std::unique_ptr<Trigger> release()                     { return std::make_unique<ReleaseTrigger>(); }
inline std::unique_ptr<Trigger> down()                        { return std::make_unique<DownTrigger>(); }
inline std::unique_ptr<Trigger> hold(float seconds)           { auto t = std::make_unique<HoldTrigger>(); t->holdSeconds   = seconds;    return t; }
inline std::unique_ptr<Trigger> tap(float maxSeconds = 0.25f) { auto t = std::make_unique<TapTrigger>();  t->tapMaxSeconds = maxSeconds; return t; }

// Implicitly convertible to Action so it slots straight into `registry.add(make(...))`.
class ActionBuilder {
public:
	explicit ActionBuilder(std::string id) { mAction.id = std::move(id); }

	ActionBuilder& label(std::string s)                         { mAction.label = std::move(s); return *this; }
	ActionBuilder& context(std::string ctx)                     { mAction.context = std::move(ctx); return *this; }
	ActionBuilder& valueType(ValueType vt)                      { mAction.valueType = vt; return *this; }
	ActionBuilder& handler(ActionHandler h)                     { mAction.handler = std::move(h); return *this; }

	// `trigger` defaults to PressTrigger when null.
	ActionBuilder& binding(std::unique_ptr<Binding> b, std::unique_ptr<Trigger> trigger = nullptr)
	{
		if (!trigger) trigger = press();
		mAction.defaultBindings.push_back(Action::BindingEntry{std::move(b), std::move(trigger)});
		return *this;
	}

	Action build() { return std::move(mAction); }
	operator Action() { return std::move(mAction); }

private:
	Action mAction;
};

inline ActionBuilder make(std::string id) { return ActionBuilder(std::move(id)); }

}
