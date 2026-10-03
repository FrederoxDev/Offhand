#pragma once
#include "Action.hpp"
#include "Device.hpp"
#include "Types.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Spyglass::Input {

/**
Per-device snapshot of every observed control's value, plus current KB
modifier mask. Bindings read this during evaluate(); Dispatcher writes
it from feed(). The mutators are public for direct Dispatcher access;
downstream consumers should treat InputState as read-only.
*/
class InputState {
public:
	float value(DeviceId device, int control) const;
	bool  isHeld(DeviceId device, int control) const;
	int   keyboardModifiers() const { return mKbMods; }

	void setValue(DeviceId device, int control, float v);
	void setKeyboardModifiers(int mods) { mKbMods = mods; }
	void clear();

private:
	struct DeviceHash { size_t operator()(const DeviceId&) const noexcept; };
	std::unordered_map<DeviceId, std::unordered_map<int, float>, DeviceHash> mValues;
	int mKbMods = 0;
};

class Registry {
public:
	void add(Action a);
	const Action* find(std::string_view id) const;
	Action*       find(std::string_view id);
	const std::vector<Action>& all() const { return mActions; }

private:
	std::vector<Action> mActions;
};

/**
Persistable user-side binding overrides. Setting an override replaces
an Action's entire defaultBindings list; an action with no bindings is
unbound. Serialized as a tagged JSON object.
*/
class Keymap {
public:
	void                                  setOverride(std::string_view actionId, std::vector<Action::BindingEntry> bindings);
	void                                  clearOverride(std::string_view actionId);
	bool                                  hasOverride(std::string_view actionId) const;
	std::span<const Action::BindingEntry> bindingsFor(const Action&) const;

	bool        loadJson(std::string_view json);
	std::string saveJson() const;

private:
	std::unordered_map<std::string, std::vector<Action::BindingEntry>> mOverrides;
};

/**
LIFO stack of context names. Action matching considers globals (empty
context) plus actions whose context appears in the stack; the topmost
matching frame wins when several actions share a chord.
*/
class ContextStack {
public:
	void push(std::string ctx);
	void pop();
	void clear();
	bool empty() const;
	const std::string& top() const;

	bool contains(std::string_view ctx) const;
	int  frameIndex(std::string_view ctx) const;
	std::span<const std::string> frames() const;

	// RAII helper: pushes on construct, pops on destroy.
	class Scope {
	public:
		Scope(ContextStack& s, std::string ctx);
		~Scope();
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
	private:
		ContextStack* mStack;
	};

private:
	std::vector<std::string> mFrames;
};

/**
Live state of an in-progress multi-step chord sequence — returned by
Dispatcher::inProgressChord() so the status bar can show e.g. "Ctrl+K …"
with the list of possible continuations. Default-constructed (`active`
false) when nothing is partway.
*/
struct InProgressChord {
	bool        active              = false;
	int         currentStep         = 0;
	int         totalSteps          = 0;
	float       secondsUntilTimeout = 0.0f;
	std::string completedPrefix;

	// (chord display, action id) pairs — every sequence that could still complete.
	std::vector<std::pair<std::string, std::string>> candidates;
};

/**
Drives the whole pipeline. Hosts call feed() as raw events arrive and
tick() once per frame; Dispatcher updates Triggers, advances chord
sequences, and invokes action handlers.
*/
class Dispatcher {
public:
	Dispatcher(Registry& r, Keymap& k, ContextStack& s);

	void feed(const InputEvent& ev);
	void tick(float dt);

	InProgressChord inProgressChord() const;
	std::vector<const Action*> visibleActions() const;

	const InputState& state() const { return mState; }

	// Rebind capture: while armed, the next non-modifier press is swallowed
	// (it fires no action) and stashed so a preferences UI can read the chord.
	void beginCapture();
	bool takeCapture(int& control, int& modifiers);
	void cancelCapture();

private:
	void  fireAction(const Action& a) const;
	bool  controlRefMatches(const ControlRef& ref, DeviceId device, int control, int modifiers) const;
	void  handleSequencePress(DeviceId device, int control, int modifiers);
	bool  isModifierKey(int control) const;

	Registry*     mReg;
	Keymap*       mMap;
	ContextStack* mStack;
	InputState    mState;

	// Chord-sequence runtime state.
	struct SequenceProgress {
		const Action* action       = nullptr;
		size_t        bindingIdx   = 0;
		size_t        currentStep  = 0;   // 0 = first step already consumed
		float         secondsLeft  = 0.0f;
	};
	std::optional<SequenceProgress> mActiveSequence;

	// Rising-edge buffer drained by tick().
	struct PendingPress { DeviceId device; int control; int modifiers; };
	std::vector<PendingPress> mPendingPresses;

	// Rebind capture state (see beginCapture()).
	bool mCapturing         = false;
	bool mCaptured          = false;
	int  mCapturedControl   = 0;
	int  mCapturedModifiers = 0;
};

}
