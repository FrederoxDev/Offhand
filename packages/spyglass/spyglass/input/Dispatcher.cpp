#include "Dispatcher.hpp"
#include "Binding.hpp"
#include "Trigger.hpp"
#include "Processor.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace Spyglass::Input {

// ============================================================================
// InputState
// ============================================================================

size_t InputState::DeviceHash::operator()(const DeviceId& d) const noexcept
{
	return std::hash<int>{}(static_cast<int>(d.kind)) ^ (std::hash<int>{}(d.index) << 1);
}

float InputState::value(DeviceId device, int control) const
{
	auto it = mValues.find(device);
	if (it == mValues.end()) return 0.0f;
	auto jt = it->second.find(control);
	if (jt == it->second.end()) return 0.0f;
	return jt->second;
}

bool InputState::isHeld(DeviceId device, int control) const
{
	return value(device, control) > 0.5f;
}

void InputState::setValue(DeviceId device, int control, float v)
{
	mValues[device][control] = v;
}

void InputState::clear()
{
	mValues.clear();
	mKbMods = 0;
}

// ============================================================================
// Registry
// ============================================================================

void Registry::add(Action a)
{
	if (find(a.id)) return;
	mActions.emplace_back(std::move(a));
}

const Action* Registry::find(std::string_view id) const
{
	for (const auto& a : mActions) if (a.id == id) return &a;
	return nullptr;
}

Action* Registry::find(std::string_view id)
{
	for (auto& a : mActions) if (a.id == id) return &a;
	return nullptr;
}

// ============================================================================
// ContextStack
// ============================================================================

void ContextStack::push(std::string ctx) { mFrames.emplace_back(std::move(ctx)); }
void ContextStack::pop()                  { if (!mFrames.empty()) mFrames.pop_back(); }
void ContextStack::clear()                { mFrames.clear(); }
bool ContextStack::empty() const          { return mFrames.empty(); }
const std::string& ContextStack::top() const { return mFrames.back(); }

bool ContextStack::contains(std::string_view ctx) const
{
	for (const auto& f : mFrames) if (f == ctx) return true;
	return false;
}

int ContextStack::frameIndex(std::string_view ctx) const
{
	for (int i = 0; i < static_cast<int>(mFrames.size()); ++i) {
		if (mFrames[i] == ctx) return i;
	}
	return -1;
}

std::span<const std::string> ContextStack::frames() const { return mFrames; }

ContextStack::Scope::Scope(ContextStack& s, std::string ctx) : mStack(&s) { mStack->push(std::move(ctx)); }
ContextStack::Scope::~Scope()                                              { if (mStack) mStack->pop(); }

// ============================================================================
// JSON serialization helpers
// ============================================================================

namespace {

const char* deviceKindName(DeviceKind k)
{
	switch (k) {
		case DeviceKind::KeyboardMouse: return "kbm";
		case DeviceKind::Gamepad:       return "gamepad";
		case DeviceKind::Touch:         return "touch";
		case DeviceKind::Custom:        return "custom";
	}
	return "kbm";
}

DeviceKind deviceKindFromName(std::string_view s)
{
	if (s == "gamepad") return DeviceKind::Gamepad;
	if (s == "touch")   return DeviceKind::Touch;
	if (s == "custom")  return DeviceKind::Custom;
	return DeviceKind::KeyboardMouse;
}

nlohmann::json toJson(const DeviceId& d)
{
	return nlohmann::json{{"kind", deviceKindName(d.kind)}, {"index", d.index}};
}

DeviceId deviceFromJson(const nlohmann::json& j)
{
	return DeviceId{deviceKindFromName(j.value("kind", "kbm")), j.value("index", 0)};
}

nlohmann::json toJson(const ControlRef& r)
{
	return nlohmann::json{{"device", toJson(r.device)}, {"control", r.control}, {"modifiers", r.modifiers}};
}

ControlRef controlRefFromJson(const nlohmann::json& j)
{
	ControlRef r;
	if (j.contains("device")) r.device = deviceFromJson(j["device"]);
	r.control   = j.value("control",   0);
	r.modifiers = j.value("modifiers", 0);
	return r;
}

std::unique_ptr<Processor> processorFromJson(const nlohmann::json& j);
std::unique_ptr<Trigger>   triggerFromJson  (const nlohmann::json& j);
std::unique_ptr<Binding>   bindingFromJson  (const nlohmann::json& j);

nlohmann::json toJson(const Processor& p);
nlohmann::json toJson(const Trigger& t);
nlohmann::json toJson(const Binding& b);

// --- Processor ---

std::unique_ptr<Processor> processorFromJson(const nlohmann::json& j)
{
	const std::string type = j.value("type", "");
	if (type == "deadzone") {
		auto p = std::make_unique<DeadzoneProcessor>();
		p->inner     = j.value("inner",     0.0f);
		p->outer     = j.value("outer",     1.0f);
		p->normalize = j.value("normalize", true);
		return p;
	}
	if (type == "scale") {
		auto p = std::make_unique<ScaleProcessor>();
		p->scale.x = j.value("x", 1.0f);
		p->scale.y = j.value("y", 1.0f);
		p->scale.z = j.value("z", 1.0f);
		return p;
	}
	if (type == "invert") {
		auto p = std::make_unique<InvertProcessor>();
		p->invertX = j.value("x", false);
		p->invertY = j.value("y", false);
		p->invertZ = j.value("z", false);
		return p;
	}
	return nullptr;
}

nlohmann::json toJson(const Processor& p)
{
	if (auto* dp = dynamic_cast<const DeadzoneProcessor*>(&p))
		return {{"type","deadzone"},{"inner",dp->inner},{"outer",dp->outer},{"normalize",dp->normalize}};
	if (auto* sp = dynamic_cast<const ScaleProcessor*>(&p))
		return {{"type","scale"},{"x",sp->scale.x},{"y",sp->scale.y},{"z",sp->scale.z}};
	if (auto* ip = dynamic_cast<const InvertProcessor*>(&p))
		return {{"type","invert"},{"x",ip->invertX},{"y",ip->invertY},{"z",ip->invertZ}};
	return {{"type","unknown"}};
}

// --- Trigger ---

std::unique_ptr<Trigger> triggerFromJson(const nlohmann::json& j)
{
	const std::string type = j.value("type", "press");
	if (type == "press")   return std::make_unique<PressTrigger>();
	if (type == "release") return std::make_unique<ReleaseTrigger>();
	if (type == "down")    return std::make_unique<DownTrigger>();
	if (type == "hold")    {
		auto t = std::make_unique<HoldTrigger>();
		t->holdSeconds = j.value("seconds", 0.5f);
		return t;
	}
	if (type == "tap") {
		auto t = std::make_unique<TapTrigger>();
		t->tapMaxSeconds = j.value("max_seconds", 0.25f);
		return t;
	}
	return std::make_unique<PressTrigger>();
}

nlohmann::json toJson(const Trigger& t)
{
	if (dynamic_cast<const PressTrigger*>(&t))   return {{"type","press"}};
	if (dynamic_cast<const ReleaseTrigger*>(&t)) return {{"type","release"}};
	if (dynamic_cast<const DownTrigger*>(&t))    return {{"type","down"}};
	if (auto* h = dynamic_cast<const HoldTrigger*>(&t))
		return {{"type","hold"},{"seconds",h->holdSeconds}};
	if (auto* tp = dynamic_cast<const TapTrigger*>(&t))
		return {{"type","tap"},{"max_seconds",tp->tapMaxSeconds}};
	return {{"type","press"}};
}

// --- Binding ---

std::vector<std::unique_ptr<Processor>> processorsFromJson(const nlohmann::json& arr)
{
	std::vector<std::unique_ptr<Processor>> out;
	if (!arr.is_array()) return out;
	for (const auto& j : arr) {
		if (auto p = processorFromJson(j)) out.emplace_back(std::move(p));
	}
	return out;
}

nlohmann::json processorsToJson(const std::vector<std::unique_ptr<Processor>>& ps)
{
	auto arr = nlohmann::json::array();
	for (const auto& p : ps) arr.push_back(toJson(*p));
	return arr;
}

std::unique_ptr<Binding> bindingFromJson(const nlohmann::json& j)
{
	const std::string type = j.value("type", "");
	if (type == "chord") {
		auto b = std::make_unique<ChordBinding>();
		b->chord = controlRefFromJson(j);
		return b;
	}
	if (type == "sequence") {
		auto b = std::make_unique<ChordSequenceBinding>();
		if (j.contains("steps") && j["steps"].is_array()) {
			for (const auto& step : j["steps"]) {
				b->steps.push_back(controlRefFromJson(step));
			}
		}
		b->stepTimeoutSeconds = j.value("timeout", 1.0f);
		return b;
	}
	if (type == "axis") {
		auto b = std::make_unique<AxisBinding>();
		b->control    = controlRefFromJson(j);
		if (j.contains("processors")) b->processors = processorsFromJson(j["processors"]);
		return b;
	}
	if (type == "composite2d") {
		auto b = std::make_unique<Composite2DBinding>();
		if (j.contains("x")) b->x = bindingFromJson(j["x"]);
		if (j.contains("y")) b->y = bindingFromJson(j["y"]);
		if (j.contains("processors")) b->processors = processorsFromJson(j["processors"]);
		return b;
	}
	if (type == "directional") {
		auto b = std::make_unique<DirectionalCompositeBinding>();
		if (j.contains("up"))    b->up    = bindingFromJson(j["up"]);
		if (j.contains("down"))  b->down  = bindingFromJson(j["down"]);
		if (j.contains("left"))  b->left  = bindingFromJson(j["left"]);
		if (j.contains("right")) b->right = bindingFromJson(j["right"]);
		if (j.contains("processors")) b->processors = processorsFromJson(j["processors"]);
		return b;
	}
	return nullptr;
}

nlohmann::json toJson(const Binding& b)
{
	if (auto* cb = dynamic_cast<const ChordBinding*>(&b)) {
		auto out = toJson(cb->chord);
		out["type"] = "chord";
		return out;
	}
	if (auto* sb = dynamic_cast<const ChordSequenceBinding*>(&b)) {
		nlohmann::json out;
		out["type"]    = "sequence";
		out["timeout"] = sb->stepTimeoutSeconds;
		auto arr = nlohmann::json::array();
		for (const auto& s : sb->steps) arr.push_back(toJson(s));
		out["steps"]   = std::move(arr);
		return out;
	}
	if (auto* ab = dynamic_cast<const AxisBinding*>(&b)) {
		auto out = toJson(ab->control);
		out["type"]       = "axis";
		out["processors"] = processorsToJson(ab->processors);
		return out;
	}
	if (auto* cp = dynamic_cast<const Composite2DBinding*>(&b)) {
		nlohmann::json out;
		out["type"]       = "composite2d";
		if (cp->x) out["x"] = toJson(*cp->x);
		if (cp->y) out["y"] = toJson(*cp->y);
		out["processors"] = processorsToJson(cp->processors);
		return out;
	}
	if (auto* dp = dynamic_cast<const DirectionalCompositeBinding*>(&b)) {
		nlohmann::json out;
		out["type"]       = "directional";
		if (dp->up)    out["up"]    = toJson(*dp->up);
		if (dp->down)  out["down"]  = toJson(*dp->down);
		if (dp->left)  out["left"]  = toJson(*dp->left);
		if (dp->right) out["right"] = toJson(*dp->right);
		out["processors"] = processorsToJson(dp->processors);
		return out;
	}
	return {{"type","unknown"}};
}

}

// ============================================================================
// Keymap
// ============================================================================

void Keymap::setOverride(std::string_view actionId, std::vector<Action::BindingEntry> bindings)
{
	mOverrides[std::string(actionId)] = std::move(bindings);
}

void Keymap::clearOverride(std::string_view actionId)
{
	mOverrides.erase(std::string(actionId));
}

bool Keymap::hasOverride(std::string_view actionId) const
{
	return mOverrides.find(std::string(actionId)) != mOverrides.end();
}

std::span<const Action::BindingEntry> Keymap::bindingsFor(const Action& a) const
{
	auto it = mOverrides.find(a.id);
	if (it != mOverrides.end()) return it->second;
	return a.defaultBindings;
}

bool Keymap::loadJson(std::string_view json)
{
	try {
		auto j = nlohmann::json::parse(json);
		if (!j.is_object() || !j.contains("overrides") || !j["overrides"].is_object()) return false;
		mOverrides.clear();
		for (auto it = j["overrides"].begin(); it != j["overrides"].end(); ++it) {
			std::vector<Action::BindingEntry> entries;
			const auto& v = it.value();
			if (!v.is_object() || !v.contains("bindings") || !v["bindings"].is_array()) continue;
			for (const auto& be : v["bindings"]) {
				Action::BindingEntry entry;
				if (be.contains("binding")) entry.binding = bindingFromJson(be["binding"]);
				if (be.contains("trigger")) entry.trigger = triggerFromJson(be["trigger"]);
				if (!entry.trigger) entry.trigger = std::make_unique<PressTrigger>();
				if (entry.binding) entries.emplace_back(std::move(entry));
			}
			mOverrides[it.key()] = std::move(entries);
		}
		return true;
	}
	catch (...) {
		return false;
	}
}

std::string Keymap::saveJson() const
{
	nlohmann::json overrides = nlohmann::json::object();
	for (const auto& [id, entries] : mOverrides) {
		auto bindings = nlohmann::json::array();
		for (const auto& e : entries) {
			nlohmann::json be;
			if (e.binding) be["binding"] = toJson(*e.binding);
			if (e.trigger) be["trigger"] = toJson(*e.trigger);
			bindings.push_back(std::move(be));
		}
		nlohmann::json entry;
		entry["bindings"] = std::move(bindings);
		overrides[id] = std::move(entry);
	}
	nlohmann::json out;
	out["overrides"] = std::move(overrides);
	return out.dump(2);
}

// ============================================================================
// Dispatcher
// ============================================================================

Dispatcher::Dispatcher(Registry& r, Keymap& k, ContextStack& s)
	: mReg(&r), mMap(&k), mStack(&s) {}

bool Dispatcher::isModifierKey(int control) const
{
	// Win32 VK_* values. We treat anything that participates in the modifier
	// mask as "not a sequence step" so users can hold Ctrl while pressing K.
	switch (control) {
		case 0x10: // VK_SHIFT
		case 0xA0: // VK_LSHIFT
		case 0xA1: // VK_RSHIFT
		case 0x11: // VK_CONTROL
		case 0xA2: // VK_LCONTROL
		case 0xA3: // VK_RCONTROL
		case 0x12: // VK_MENU (Alt)
		case 0xA4: // VK_LMENU
		case 0xA5: // VK_RMENU
		case 0x5B: // VK_LWIN
		case 0x5C: // VK_RWIN
			return true;
		default:
			return false;
	}
}

bool Dispatcher::controlRefMatches(const ControlRef& ref, DeviceId device, int control, int modifiers) const
{
	return ref.device == device && ref.control == control && ref.modifiers == modifiers;
}

void Dispatcher::feed(const InputEvent& ev)
{
	const float prev = mState.value(ev.device, ev.control);
	const bool wasActive = prev > 0.5f;
	const bool nowActive = ev.value > 0.5f;

	mState.setValue(ev.device, ev.control, ev.value);
	if (ev.device.kind == DeviceKind::KeyboardMouse) {
		mState.setKeyboardModifiers(ev.modifiers);
	}

	if (!wasActive && nowActive && !isModifierKey(ev.control)) {
		if (mCapturing) {
			mCapturedControl   = ev.control;
			mCapturedModifiers = ev.modifiers;
			mCaptured          = true;
			mCapturing         = false;
			return;
		}
		mPendingPresses.push_back({ev.device, ev.control, ev.modifiers});
	}
}

void Dispatcher::beginCapture()
{
	mCapturing = true;
	mCaptured  = false;
}

bool Dispatcher::takeCapture(int& control, int& modifiers)
{
	if (!mCaptured) return false;
	control    = mCapturedControl;
	modifiers  = mCapturedModifiers;
	mCaptured  = false;
	return true;
}

void Dispatcher::cancelCapture()
{
	mCapturing = false;
	mCaptured  = false;
}

namespace {

bool actionInScope(const Action& a, const ContextStack& s)
{
	return a.context.empty() || s.contains(a.context);
}

}

void Dispatcher::fireAction(const Action& a) const
{
	if (!a.handler) return;
	ActionFireInfo info;
	info.phase = ActionPhase::Triggered;
	info.value = true;
	a.handler(info);
}

void Dispatcher::handleSequencePress(DeviceId device, int control, int modifiers)
{
	if (!mReg || !mMap || !mStack) return;

	// Try to advance the in-flight sequence first.
	if (mActiveSequence) {
		const Action& a = *mActiveSequence->action;
		auto bindings = mMap->bindingsFor(a);
		if (mActiveSequence->bindingIdx < bindings.size()) {
			const auto& entry = bindings[mActiveSequence->bindingIdx];
			if (auto* seq = dynamic_cast<const ChordSequenceBinding*>(entry.binding.get())) {
				const size_t nextStep = mActiveSequence->currentStep + 1;
				if (nextStep < seq->steps.size()
					&& controlRefMatches(seq->steps[nextStep], device, control, modifiers))
				{
					mActiveSequence->currentStep = nextStep;
					mActiveSequence->secondsLeft = seq->stepTimeoutSeconds;
					if (nextStep + 1 == seq->steps.size()) {
						fireAction(a);
						mActiveSequence.reset();
					}
					return;
				}
			}
		}
		// Wrong key while a sequence is in progress — cancel it and fall through
		// so this press can start a new sequence.
		mActiveSequence.reset();
	}

	// Try to start a new sequence whose first step matches this press.
	for (const auto& action : mReg->all()) {
		if (!actionInScope(action, *mStack)) continue;
		auto bindings = mMap->bindingsFor(action);
		for (size_t i = 0; i < bindings.size(); ++i) {
			const auto& entry = bindings[i];
			auto* seq = dynamic_cast<const ChordSequenceBinding*>(entry.binding.get());
			if (!seq || seq->steps.empty()) continue;
			if (!controlRefMatches(seq->steps[0], device, control, modifiers)) continue;

			if (seq->steps.size() == 1) {
				fireAction(action);
				mActiveSequence.reset();
			}
			else {
				SequenceProgress p;
				p.action      = &action;
				p.bindingIdx  = i;
				p.currentStep = 0;
				p.secondsLeft = seq->stepTimeoutSeconds;
				mActiveSequence = p;
			}
			return;
		}
	}
}

void Dispatcher::tick(float dt)
{
	if (!mReg || !mMap || !mStack) return;

	// Expire in-flight sequence before consuming this frame's presses; a press
	// that arrives the same tick the timeout would expire still gets a chance.
	if (mActiveSequence) {
		mActiveSequence->secondsLeft -= dt;
		if (mActiveSequence->secondsLeft <= 0.0f) mActiveSequence.reset();
	}

	for (const auto& p : mPendingPresses) {
		handleSequencePress(p.device, p.control, p.modifiers);
	}
	mPendingPresses.clear();

	for (const auto& action : mReg->all()) {
		if (!actionInScope(action, *mStack)) continue;

		auto bindings = mMap->bindingsFor(action);
		for (const auto& entry : bindings) {
			if (!entry.binding || !entry.trigger) continue;
			if (dynamic_cast<const ChordSequenceBinding*>(entry.binding.get())) continue;

			Trigger::UpdateContext ctx;
			ctx.dt        = dt;
			ctx.valueType = action.valueType;

			auto v = entry.binding->evaluate(mState);
			ctx.signalActive = v.has_value();
			if (v) ctx.rawValue = *v;

			auto result = entry.trigger->update(ctx);
			if (result.phase && action.handler) {
				ActionFireInfo info;
				info.phase   = *result.phase;
				info.value   = result.value;
				info.elapsed = result.elapsed;
				action.handler(info);
			}
		}
	}
}

InProgressChord Dispatcher::inProgressChord() const
{
	InProgressChord out;
	if (!mActiveSequence || !mReg || !mMap || !mStack) return out;

	const Action& active = *mActiveSequence->action;
	auto activeBindings  = mMap->bindingsFor(active);
	if (mActiveSequence->bindingIdx >= activeBindings.size()) return out;

	auto* activeSeq = dynamic_cast<const ChordSequenceBinding*>(
		activeBindings[mActiveSequence->bindingIdx].binding.get());
	if (!activeSeq) return out;

	out.active              = true;
	out.currentStep         = static_cast<int>(mActiveSequence->currentStep + 1);
	out.totalSteps          = static_cast<int>(activeSeq->steps.size());
	out.secondsUntilTimeout = mActiveSequence->secondsLeft;

	// Build the "Ctrl+K …" prefix from the steps already consumed.
	for (size_t i = 0; i <= mActiveSequence->currentStep && i < activeSeq->steps.size(); ++i) {
		ChordBinding step;
		step.chord = activeSeq->steps[i];
		if (!out.completedPrefix.empty()) out.completedPrefix += " ";
		out.completedPrefix += step.toString(nullptr);
	}

	// Every sequence that shares this prefix is still a live candidate.
	const size_t consumed = mActiveSequence->currentStep + 1;
	for (const auto& action : mReg->all()) {
		if (!actionInScope(action, *mStack)) continue;
		for (const auto& entry : mMap->bindingsFor(action)) {
			auto* seq = dynamic_cast<const ChordSequenceBinding*>(entry.binding.get());
			if (!seq || seq->steps.size() <= consumed) continue;

			bool prefixMatches = true;
			for (size_t i = 0; i < consumed; ++i) {
				if (!(seq->steps[i].device   == activeSeq->steps[i].device
				   && seq->steps[i].control  == activeSeq->steps[i].control
				   && seq->steps[i].modifiers == activeSeq->steps[i].modifiers))
				{
					prefixMatches = false;
					break;
				}
			}
			if (!prefixMatches) continue;

			std::string display;
			for (const auto& step : seq->steps) {
				ChordBinding cb;
				cb.chord = step;
				if (!display.empty()) display += " ";
				display += cb.toString(nullptr);
			}
			out.candidates.emplace_back(std::move(display), action.id);
		}
	}

	return out;
}

std::vector<const Action*> Dispatcher::visibleActions() const
{
	std::vector<const Action*> out;
	if (!mReg || !mStack) return out;
	for (const auto& a : mReg->all()) {
		if (a.context.empty() || mStack->contains(a.context)) out.push_back(&a);
	}
	return out;
}

}
