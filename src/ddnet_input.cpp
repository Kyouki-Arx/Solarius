#include "ddnet_input.h"

#include <godot_cpp/core/math.hpp>
#include <algorithm>

using namespace godot;

namespace godot {

float DDNetInput::get_effective_max_distance() const {
	// CControls::GetMaxMouseDistance()
	const float camera_max_distance = 200.0f;
	const float follow = follow_factor / 100.0f;
	return std::min(follow != 0.0f ? camera_max_distance / follow + deadzone : max_distance, max_distance);
}

Vector2 DDNetInput::apply_delta(const Vector2 &raw_delta) {
	// CControls::OnCursorMove: Factor = g_Config.m_InpMousesens / 100.0f
	const float factor = mouse_sens / 100.0f;
	pos += raw_delta * factor;
	clamp_mouse_pos();
	return pos;
}

void DDNetInput::clamp_mouse_pos() {
	// CControls::ClampMousePos()
	float distance = pos.length();
	if(distance < 0.001f) {
		pos.x = 0.001f;
		pos.y = 0.0f;
		distance = 0.001f;
	}
	if(distance < min_distance) {
		pos = pos.normalized() * min_distance;
	}
	distance = pos.length();
	if(distance > get_effective_max_distance()) {
		pos = pos.normalized() * get_effective_max_distance();
	}
}

} // namespace godot
