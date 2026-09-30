#pragma once

#include <godot_cpp/variant/vector2.hpp>

namespace godot {

// Port of DDNet's mouse handling. Behaviour copied from
// TClient/src/game/client/components/controls.cpp (CControls::OnCursorMove and
// CControls::ClampMousePos) and TClient/src/engine/shared/config_variables.h
// (inp_mousesens default 200, cl_mouse_max_distance).
// The original DDNet code is not modified; this is a re-implementation of the
// same formulas in Godot units.
//
// Plain struct on purpose: a value member of AimTrainer. Deriving from
// RefCounted would run Object's constructor without memnew(), which godot-cpp
// rejects ("created without binding callbacks").
struct DDNetInput {
	// inp_mousesens: 200 is "1.0" in DDNet, converted with factor = value / 100.0
	float mouse_sens = 200.0f;
	// cl_mouse_min_distance / cl_mouse_max_distance (DDNet units, 32.0 per tile).
	float min_distance = 0.0f;
	float max_distance = 200.0f * 32.0f;
	// cl_mouse_followfactor / cl_mouse_deadzone, both divided by 100 where used.
	float follow_factor = 0.0f;
	float deadzone = 0.0f;

	// Current cursor position, centred on the player (CControls::m_aMousePos).
	Vector2 pos;

	// CControls::OnCursorMove
	Vector2 apply_delta(const Vector2 &raw_delta);

	// CControls::GetMaxMouseDistance
	float get_effective_max_distance() const;

	// CControls::ClampMousePos
	void clamp_mouse_pos();
};

} // namespace godot
