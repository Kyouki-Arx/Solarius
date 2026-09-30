#include "target.h"
#include <godot_cpp/core/math.hpp>
#include <cmath>

using namespace godot;

namespace godot {

float ease_value(MoveEase e, float t) {
	t = CLAMP(t, 0.0f, 1.0f);
	switch(e) {
		case MoveEase::LINEAR:
			return t;
		case MoveEase::EASE_OUT:
			return 1.0f - std::pow(1.0f - t, 3.0f);
		case MoveEase::EASE_IN:
			return t * t * t;
		case MoveEase::EASE_IN_OUT:
			return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
		case MoveEase::BOUNCE_IN: {
			// Accelerating into motion: slow start, fast finish.
			const float c = 1.70158f;
			return 1.0f - std::pow(1.0f - t, 2.0f) * (1.0f - t * c);
		}
		case MoveEase::BOUNCE_OUT:
			// Decelerating to a stop: fast start, slow finish.
			return 1.0f - std::pow(1.0f - t, 2.0f);
	}
	return t;
}

} // namespace godot

Vector2 Target::current_velocity() const {
	if(!moving || move_duration <= 0.0f) {
		return Vector2();
	}
	// Numerical derivative of eased progress over a small window, used to shade
	// the trajectory line (dim while decelerating, bright while speeding up).
	const float h = 0.02f;
	const float t0 = CLAMP(move_time, 0.0f, move_duration);
	const float t1 = CLAMP(move_time + h, 0.0f, move_duration);
	const float rate = (ease_value(ease, t1 / move_duration) - ease_value(ease, t0 / move_duration)) * (move_duration / h);
	return move_dir * move_speed * rate;
}

float Target::point_in_distance(const Vector2 &p) const {
	return (p - pos).length();
}

bool Target::contains(const Vector2 &p) const {
	const Vector2 d = p - pos;
	if(shape == TargetShape::CIRCLE) {
		return d.length() <= radius;
	}
	return std::fabs(d.x) <= radius && std::fabs(d.y) <= radius;
}
