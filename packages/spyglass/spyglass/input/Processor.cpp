#include "Processor.hpp"

#include <algorithm>
#include <cmath>

namespace Spyglass::Input {

float DeadzoneProcessor::process1D(float v) const
{
	const float mag = std::abs(v);
	if (mag <= inner) return 0.0f;
	if (!normalize) return v;
	const float span = (outer - inner);
	if (span <= 0.0f) return v;
	const float t = std::clamp((mag - inner) / span, 0.0f, 1.0f);
	return std::copysign(t, v);
}

Vec2 DeadzoneProcessor::process2D(Vec2 v) const
{
	const float mag = std::sqrt(v.x * v.x + v.y * v.y);
	if (mag <= inner) return Vec2{};
	if (!normalize) return v;
	const float span = (outer - inner);
	if (span <= 0.0f) return v;
	const float t = std::clamp((mag - inner) / span, 0.0f, 1.0f);
	const float s = (mag > 0.0f) ? (t / mag) : 0.0f;
	return Vec2{v.x * s, v.y * s};
}

float ScaleProcessor::process1D(float v) const { return v * scale.x; }
Vec2  ScaleProcessor::process2D(Vec2 v)  const { return Vec2{v.x * scale.x, v.y * scale.y}; }
Vec3  ScaleProcessor::process3D(Vec3 v)  const { return Vec3{v.x * scale.x, v.y * scale.y, v.z * scale.z}; }

float InvertProcessor::process1D(float v) const { return invertX ? -v : v; }
Vec2  InvertProcessor::process2D(Vec2 v)  const { return Vec2{invertX ? -v.x : v.x, invertY ? -v.y : v.y}; }
Vec3  InvertProcessor::process3D(Vec3 v)  const { return Vec3{invertX ? -v.x : v.x, invertY ? -v.y : v.y, invertZ ? -v.z : v.z}; }

}
