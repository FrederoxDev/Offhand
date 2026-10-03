#pragma once
#include "Types.hpp"

#include <optional>

namespace Spyglass::Input {

/**
Stateful gate between a Binding's per-frame value and an Action handler.
One Trigger instance per (Action, BindingEntry). Dispatcher updates them
each tick; the returned phase (if any) is fired on the action handler.
*/
class Trigger {
public:
	struct UpdateContext {
		float       dt           = 0.0f;
		bool        signalActive = false;
		ActionValue rawValue;
		ValueType   valueType    = ValueType::Bool;
	};

	struct UpdateResult {
		std::optional<ActionPhase> phase;
		ActionValue                value;
		float                      elapsed = 0.0f;
	};

	virtual ~Trigger() = default;
	virtual UpdateResult update(const UpdateContext& ctx) = 0;
	virtual void reset() = 0;
};

// Fires Triggered once on the rising edge.
class PressTrigger : public Trigger {
public:
	UpdateResult update(const UpdateContext&) override;
	void reset() override;
private:
	bool mWasActive = false;
};

// Fires Triggered once on the falling edge.
class ReleaseTrigger : public Trigger {
public:
	UpdateResult update(const UpdateContext&) override;
	void reset() override;
private:
	bool mWasActive = false;
};

/**
Started on press, Triggered every frame held, Completed on release.
Use this for "while held" semantics (walk while W, pan while RMB).
*/
class DownTrigger : public Trigger {
public:
	UpdateResult update(const UpdateContext&) override;
	void reset() override;
private:
	bool  mWasActive = false;
	float mElapsed   = 0.0f;
};

/**
Triggered once after `holdSeconds` of continuous active signal. Emits
Canceled if released before the threshold; Completed if the threshold
was reached and then released.
*/
class HoldTrigger : public Trigger {
public:
	float holdSeconds = 0.5f;

	UpdateResult update(const UpdateContext&) override;
	void reset() override;
private:
	bool  mWasActive = false;
	bool  mFired     = false;
	float mElapsed   = 0.0f;
};

/**
Triggered on release iff the held duration was shorter than
`tapMaxSeconds`. Used to disambiguate tap-vs-hold on the same chord.
*/
class TapTrigger : public Trigger {
public:
	float tapMaxSeconds = 0.25f;

	UpdateResult update(const UpdateContext&) override;
	void reset() override;
private:
	bool  mWasActive = false;
	float mElapsed   = 0.0f;
};

}
