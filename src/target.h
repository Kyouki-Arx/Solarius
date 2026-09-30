#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace godot {

enum class TargetShape { CIRCLE, SQUARE };
// Ease types for moving targets.
enum class MoveEase { LINEAR, EASE_OUT, EASE_IN, EASE_IN_OUT, BOUNCE_IN, BOUNCE_OUT };

// Movement easing, shared by the simulation and the trajectory renderer.
float ease_value(MoveEase e, float t);

struct Target {
	bool active = false;
	bool gray = false; // gray = hold RMB, white = click LMB
	TargetShape shape = TargetShape::CIRCLE;
	Vector2 pos;
	Vector2 start_pos;
	float radius = 40.0f; // half-extent for squares, radius for circles
	float hold_time = 1.0f; // required hold duration (seconds)
	float life = 0.0f;
	float age = 0.0f;

	// Movement
	bool moving = false;
	float move_duration = 0.0f;
	float move_time = 0.0f;
	Vector2 move_dir;
	float move_speed = 0.0f;
	MoveEase ease = MoveEase::LINEAR;
	// Random retarget mid-flight.
	float retarget_chance_per_s = 0.0f;

	// Runtime hold state
	bool holding = false;
	float hold_progress = 0.0f;
	float hold_elapsed = 0.0f;
	// Release window for this target, scaled with the difficulty ramp (a shorter
	// required hold gets a proportionally tighter window).
	float hold_tolerance = 0.5f;

	Vector2 current_velocity() const;
	float point_in_distance(const Vector2 &p) const;
	bool contains(const Vector2 &p) const;
};

} // namespace godot
