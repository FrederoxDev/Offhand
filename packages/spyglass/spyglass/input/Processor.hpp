#pragma once
#include "Types.hpp"

namespace Spyglass::Input {

// Post-processing pass applied to a binding's value before it reaches the
// action. Subclasses override whichever overloads match their value shape.
class Processor {
public:
	virtual ~Processor() = default;

	virtual float process1D(float v) const { return v; }
	virtual Vec2  process2D(Vec2 v)  const { return v; }
	virtual Vec3  process3D(Vec3 v)  const { return v; }
};

/**
Zeros values inside an inner deadzone and (when `normalize` is true)
rescales the active region between `inner` and `outer` to 0..1.
*/
class DeadzoneProcessor : public Processor {
public:
	float inner     = 0.0f;
	float outer     = 1.0f;
	bool  normalize = true;

	float process1D(float v) const override;
	Vec2  process2D(Vec2 v)  const override;
};

class ScaleProcessor : public Processor {
public:
	Vec3 scale{1.0f, 1.0f, 1.0f};

	float process1D(float v) const override;
	Vec2  process2D(Vec2 v)  const override;
	Vec3  process3D(Vec3 v)  const override;
};

class InvertProcessor : public Processor {
public:
	bool invertX = true;
	bool invertY = false;
	bool invertZ = false;

	float process1D(float v) const override;
	Vec2  process2D(Vec2 v)  const override;
	Vec3  process3D(Vec3 v)  const override;
};

}
