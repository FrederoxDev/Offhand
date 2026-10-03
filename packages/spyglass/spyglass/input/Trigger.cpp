#include "Trigger.hpp"

namespace Spyglass::Input {

namespace {

ActionValue passThrough(const Trigger::UpdateContext& ctx)
{
	return ctx.rawValue;
}

}

// --- PressTrigger ---

Trigger::UpdateResult PressTrigger::update(const UpdateContext& ctx)
{
	UpdateResult out;
	if (ctx.signalActive && !mWasActive) {
		out.phase = ActionPhase::Triggered;
		out.value = passThrough(ctx);
	}
	mWasActive = ctx.signalActive;
	return out;
}

void PressTrigger::reset() { mWasActive = false; }

// --- ReleaseTrigger ---

Trigger::UpdateResult ReleaseTrigger::update(const UpdateContext& ctx)
{
	UpdateResult out;
	if (!ctx.signalActive && mWasActive) {
		out.phase = ActionPhase::Triggered;
		out.value = passThrough(ctx);
	}
	mWasActive = ctx.signalActive;
	return out;
}

void ReleaseTrigger::reset() { mWasActive = false; }

// --- DownTrigger ---

Trigger::UpdateResult DownTrigger::update(const UpdateContext& ctx)
{
	UpdateResult out;
	if (ctx.signalActive) {
		if (!mWasActive) {
			out.phase = ActionPhase::Started;
			mElapsed = 0.0f;
		}
		else {
			out.phase = ActionPhase::Triggered;
			mElapsed += ctx.dt;
		}
		out.value   = passThrough(ctx);
		out.elapsed = mElapsed;
	}
	else if (mWasActive) {
		out.phase   = ActionPhase::Completed;
		out.elapsed = mElapsed;
		mElapsed    = 0.0f;
	}
	mWasActive = ctx.signalActive;
	return out;
}

void DownTrigger::reset() { mWasActive = false; mElapsed = 0.0f; }

// --- HoldTrigger ---

Trigger::UpdateResult HoldTrigger::update(const UpdateContext& ctx)
{
	UpdateResult out;
	if (ctx.signalActive) {
		if (!mWasActive) {
			out.phase = ActionPhase::Started;
			mElapsed  = 0.0f;
			mFired    = false;
		}
		else {
			mElapsed += ctx.dt;
			if (!mFired && mElapsed >= holdSeconds) {
				out.phase = ActionPhase::Triggered;
				mFired    = true;
			}
		}
		out.value   = passThrough(ctx);
		out.elapsed = mElapsed;
	}
	else if (mWasActive) {
		out.phase   = mFired ? ActionPhase::Completed : ActionPhase::Canceled;
		out.elapsed = mElapsed;
		mElapsed    = 0.0f;
		mFired      = false;
	}
	mWasActive = ctx.signalActive;
	return out;
}

void HoldTrigger::reset() { mWasActive = false; mFired = false; mElapsed = 0.0f; }

// --- TapTrigger ---

Trigger::UpdateResult TapTrigger::update(const UpdateContext& ctx)
{
	UpdateResult out;
	if (ctx.signalActive) {
		if (!mWasActive) {
			out.phase = ActionPhase::Started;
			mElapsed  = 0.0f;
		}
		else {
			mElapsed += ctx.dt;
		}
		out.elapsed = mElapsed;
	}
	else if (mWasActive) {
		if (mElapsed <= tapMaxSeconds) {
			out.phase = ActionPhase::Triggered;
			out.value = passThrough(ctx);
		}
		else {
			out.phase = ActionPhase::Canceled;
		}
		out.elapsed = mElapsed;
		mElapsed    = 0.0f;
	}
	mWasActive = ctx.signalActive;
	return out;
}

void TapTrigger::reset() { mWasActive = false; mElapsed = 0.0f; }

}
