#pragma once
#include "Binding.hpp"
#include "Trigger.hpp"
#include "Types.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Spyglass::Input {

using ActionHandler = std::function<void(const ActionFireInfo&)>;

/**
One registered action. `defaultBindings` are shipped defaults; the Keymap's
override (if any) replaces this list entirely. `context` empty = global;
otherwise the action only matches while that context is in the stack.
*/
struct Action {
	std::string id;       // convention: "category.verb", e.g. "playback.toggle"
	std::string label;
	std::string context;
	ValueType   valueType = ValueType::Bool;

	struct BindingEntry {
		std::unique_ptr<Binding> binding;
		std::unique_ptr<Trigger> trigger;  // null = PressTrigger by default
	};
	std::vector<BindingEntry> defaultBindings;

	ActionHandler handler;
};

}
