#include "aim_trainer.h"
#include "register_types.h"

#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace godot {

Input *inp() { return Input::get_singleton(); }

} // namespace godot

// ---------------------------------------------------------------------------
// Laser beam geometry
// ---------------------------------------------------------------------------
//
// The beam leaves the player point (screen centre), passes through the cursor
// and keeps going. A shot resolves against the closest silhouette the beam
// crosses, so hit detection is a ray/silhouette test rather than a
// point-in-shape test.

// Entry and exit parameters of a ray against a target silhouette, in units of
// the (unit-length) ray direction. `t_near` may be negative when the origin is
// already inside the shape. Returns false when the ray misses entirely.
//
// `center` is the silhouette's centre in the SAME space as `origin`/`dir`
// (screen pixels). Passing the raw target position here would mix DDNet units
// with pixels and make every ray miss.
static bool ray_shape_span(const Target &t, const Vector2 &center, const Vector2 &origin,
		const Vector2 &dir, float &t_near, float &t_far) {
	if(t.shape == TargetShape::CIRCLE) {
		const Vector2 oc = origin - center;
		const float b = oc.dot(dir);
		const float c = oc.length_squared() - t.radius * t.radius;
		const float disc = b * b - c;
		if(disc < 0.0f) {
			return false;
		}
		const float s = sqrtf(disc);
		t_near = -b - s;
		t_far = -b + s;
		return true;
	}
	// Square: axis-aligned slab test.
	float tmin = -1e30f;
	float tmax = 1e30f;
	for(int axis = 0; axis < 2; axis++) {
		const float o = axis == 0 ? origin.x : origin.y;
		const float d = axis == 0 ? dir.x : dir.y;
		const float c = axis == 0 ? center.x : center.y;
		const float lo = c - t.radius;
		const float hi = c + t.radius;
		if(std::fabs(d) < 1e-6f) {
			if(o < lo || o > hi) {
				return false;
			}
			continue;
		}
		float t0 = (lo - o) / d;
		float t1 = (hi - o) / d;
		if(t0 > t1) {
			const float tmp = t0;
			t0 = t1;
			t1 = tmp;
		}
		tmin = MAX(tmin, t0);
		tmax = MIN(tmax, t1);
	}
	if(tmax < tmin) {
		return false;
	}
	t_near = tmin;
	t_far = tmax;
	return true;
}

AimTrainer::AimTrainer() {
	player_pos = Vector2(0, 0);
}

void AimTrainer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("reset_session"), &AimTrainer::reset_session);

	// Test/debug hooks.
	ClassDB::bind_method(D_METHOD("get_target_count"), &AimTrainer::get_target_count);
	ClassDB::bind_method(D_METHOD("get_active_target_count"), &AimTrainer::get_active_target_count);
	ClassDB::bind_method(D_METHOD("get_cursor_pos"), &AimTrainer::get_cursor_pos);
	ClassDB::bind_method(D_METHOD("get_score"), &AimTrainer::get_score);
	ClassDB::bind_method(D_METHOD("get_miss_count"), &AimTrainer::get_miss_count);
	ClassDB::bind_method(D_METHOD("get_hit_count"), &AimTrainer::get_hit_count);
	ClassDB::bind_method(D_METHOD("get_bare_shot_count"), &AimTrainer::get_bare_shot_count);
	ClassDB::bind_method(D_METHOD("get_shot_count"), &AimTrainer::get_shot_count);
	ClassDB::bind_method(D_METHOD("get_accuracy"), &AimTrainer::get_accuracy);
	ClassDB::bind_method(D_METHOD("get_play_time"), &AimTrainer::get_play_time);
	ClassDB::bind_method(D_METHOD("get_sens"), &AimTrainer::get_sens);
	ClassDB::bind_method(D_METHOD("get_maxdist"), &AimTrainer::get_maxdist);
	ClassDB::bind_method(D_METHOD("is_laser_locked"), &AimTrainer::is_laser_locked);
	ClassDB::bind_method(D_METHOD("laser_tip_dir"), &AimTrainer::laser_tip_dir);
	ClassDB::bind_method(D_METHOD("laser_reach_px"), &AimTrainer::laser_reach_px);
	ClassDB::bind_method(D_METHOD("debug_laser_has_target"), &AimTrainer::debug_laser_has_target);
	ClassDB::bind_method(D_METHOD("debug_set_cursor"), &AimTrainer::debug_set_cursor);
	ClassDB::bind_method(D_METHOD("debug_spawn_color"), &AimTrainer::debug_spawn_color);
	ClassDB::bind_method(D_METHOD("debug_clear_targets"), &AimTrainer::debug_clear_targets);
	ClassDB::bind_method(D_METHOD("debug_is_gray"), &AimTrainer::debug_is_gray);
	ClassDB::bind_method(D_METHOD("debug_active"), &AimTrainer::debug_active);
	ClassDB::bind_method(D_METHOD("debug_first_hold_ms"), &AimTrainer::debug_first_hold_ms);
	ClassDB::bind_method(D_METHOD("debug_freeze_motion"), &AimTrainer::debug_freeze_motion);
	ClassDB::bind_method(D_METHOD("debug_holding_duration_s"), &AimTrainer::debug_holding_duration_s);
	ClassDB::bind_method(D_METHOD("debug_tuning"), &AimTrainer::debug_tuning);
	ClassDB::bind_method(D_METHOD("debug_config_keys"), &AimTrainer::debug_config_keys);
	ClassDB::bind_method(D_METHOD("debug_menu_rows"), &AimTrainer::debug_menu_rows);
	ClassDB::bind_method(D_METHOD("debug_menu_select"), &AimTrainer::debug_menu_select);
	ClassDB::bind_method(D_METHOD("debug_menu_adjust"), &AimTrainer::debug_menu_adjust);
	ClassDB::bind_method(D_METHOD("debug_close_menu"), &AimTrainer::debug_close_menu);
	ClassDB::bind_method(D_METHOD("debug_is_editing"), &AimTrainer::debug_is_editing);
	ClassDB::bind_method(D_METHOD("debug_edit_buffer"), &AimTrainer::debug_edit_buffer);
	ClassDB::bind_method(D_METHOD("debug_farthest_target"), &AimTrainer::debug_farthest_target);
	ClassDB::bind_method(D_METHOD("debug_spawn_reach"), &AimTrainer::debug_spawn_reach);
	ClassDB::bind_method(D_METHOD("debug_any_target_unreachable"), &AimTrainer::debug_any_target_unreachable);
	ClassDB::bind_method(D_METHOD("debug_break_hold"), &AimTrainer::debug_break_hold);
	ClassDB::bind_method(D_METHOD("debug_suppress_spawn"), &AimTrainer::debug_suppress_spawn);
	ClassDB::bind_method(D_METHOD("debug_target_dump"), &AimTrainer::debug_target_dump);
	ClassDB::bind_method(D_METHOD("debug_set_tuning"), &AimTrainer::debug_set_tuning);
	ClassDB::bind_method(D_METHOD("debug_spawn_area"), &AimTrainer::debug_spawn_area);
	ClassDB::bind_method(D_METHOD("debug_simulate_focus_loss"), &AimTrainer::debug_simulate_focus_loss);
	ClassDB::bind_method(D_METHOD("debug_set_session_time"), &AimTrainer::debug_set_session_time);
	ClassDB::bind_method(D_METHOD("debug_difficulty_level"), &AimTrainer::debug_difficulty_level);
	ClassDB::bind_method(D_METHOD("debug_set_color"), &AimTrainer::debug_set_color);
	ClassDB::bind_method(D_METHOD("get_hold_duration_ms"), &AimTrainer::get_hold_duration_ms);
	ClassDB::bind_method(D_METHOD("get_hold_progress"), &AimTrainer::get_hold_progress);
	ClassDB::bind_method(D_METHOD("debug_force_move_cursor"), &AimTrainer::debug_force_move_cursor);
	ClassDB::bind_method(D_METHOD("debug_spawn_target"), &AimTrainer::debug_spawn_target);
	ClassDB::bind_method(D_METHOD("debug_open_settings"), &AimTrainer::debug_open_settings);
	ClassDB::bind_method(D_METHOD("debug_open_results"), &AimTrainer::debug_open_results);
	ClassDB::bind_method(D_METHOD("debug_click"), &AimTrainer::debug_click);
	ClassDB::bind_method(D_METHOD("debug_prepare"), &AimTrainer::debug_prepare);
	ClassDB::bind_method(D_METHOD("sync_input_from_tuning"), &AimTrainer::sync_input_from_tuning);
	ClassDB::bind_method(D_METHOD("first_target_pos"), &AimTrainer::first_target_pos);
	ClassDB::bind_method(D_METHOD("first_target_is_gray"), &AimTrainer::first_target_is_gray);
	ClassDB::bind_method(D_METHOD("first_target_radius"), &AimTrainer::first_target_radius);
	ClassDB::bind_method(D_METHOD("debug_set_lifetime"), &AimTrainer::debug_set_lifetime);
	ClassDB::bind_method(D_METHOD("debug_reset"), &AimTrainer::debug_reset);
}

void AimTrainer::_ready() {
	// Load before anything reads the tuning, so window mode/size and input
	// limits come up already applied on the very first frame.
	load_config();
	inp()->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
	update_layout();
	init_motes();
	reset_session();
}

void AimTrainer::update_layout() {
	Vector2 size = get_viewport_rect().size;
	screen_center = size * 0.5f;
	px_per_unit = MIN(size.x / tuning.field_width, size.y / tuning.field_height);
}

Vector2 AimTrainer::to_screen(const Vector2 &p) const {
	return screen_center + p * px_per_unit;
}

Vector2 AimTrainer::to_ddnet(const Vector2 &p) const {
	return (p - screen_center) / px_per_unit;
}

void AimTrainer::reset_session() {
	stats.reset();
	targets.clear();
	pops.clear();
	play_time = 0.0f;
	session_time = 0.0f;
	session_origin = (float)Time::get_singleton()->get_ticks_msec() / 1000.0f;
	current_max_size = tuning.max_size;
	spawn_timer = 0.0f;
	spawn_interval = tuning.spawn_interval;
	rmb_held = false;
	holding_index = -1;
	hold_scored = false;
	last_interaction_time = 0.0f;
	input.pos = Vector2();
	screen = Screen::GAME;
	paused = false;
	debris.clear();
	hit_flashes.clear();
	sparks.clear();
	rings.clear();
	hit_markers.clear();
	shake_amount = 0.0f;
	shake_offset = Vector2();
	fire_flash = 0.0f;
	laser_lock = false;
	laser_dir = Vector2(1, 0);
	laser_reach = 0.0f;
	laser_end = screen_center;
	update_laser_geometry();
	for(int i = 0; i < tuning.target_count; i++) {
		spawn_target();
	}
	queue_redraw();
}

void AimTrainer::_input(const Ref<InputEvent> &event) {
	Ref<InputEventMouseMotion> mm = event;
	if(mm.is_valid()) {
		if(inp()->get_mouse_mode() == Input::MOUSE_MODE_CAPTURED) {
			// DDNet: Factor = inp_mousesens / 100.0f, then ClampMousePos()
			input.mouse_sens = tuning.mouse_sens;
			input.max_distance = tuning.mouse_max_distance;
			input.follow_factor = 0.0f;
			input.deadzone = 0.0f;
			input.min_distance = 0.0f;
			(void)input.apply_delta(mm->get_relative());
		}
		queue_redraw();
		return;
	}
	Ref<InputEventMouseButton> mb = event;
	if(mb.is_valid()) {
		if(mb->get_button_index() == MOUSE_BUTTON_LEFT && mb->is_pressed()) {
			// While RMB is held the beam is busy charging a gray target. Firing
			// LMB on top of that used to strand the charge (progress frozen,
			// target never resolved), so LMB is simply inert during a hold.
			if(!rmb_held && holding_index < 0) {
				resolve_mouse_click(input.pos, true);
			}
		} else if(mb->get_button_index() == MOUSE_BUTTON_RIGHT) {
			if(mb->is_pressed()) {
				if(!rmb_held) {
					rmb_held = true;
					// The beam decides what is under the aim, not the raw cursor
					// pixel: RMB starts a hold on the gray target it crosses and
					// counts as a miss otherwise.
					resolve_mouse_click(input.pos, false);
				}
			} else if(rmb_held) {
				rmb_held = false;
				on_hold_released(false);
			}
		}
		return;
	}
	Ref<InputEventKey> key = event;
	if(key.is_valid() && key->is_pressed() && !key->is_echo()) {
		if(key->get_keycode() == KEY_R) {
			reset_session();
		} else if(key->get_keycode() == KEY_ESCAPE) {
			if(screen == Screen::SETTINGS) {
				if(editing) {
					editing = false;
					edit_buffer = "";
				} else {
					apply_action(ACT_BACK);
				}
			} else if(screen == Screen::RESULTS) {
				reset_session();
				inp()->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
			} else {
				return_screen = Screen::GAME;
				screen = Screen::RESULTS;
				paused = true;
				inp()->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
				build_menu();
			}
		} else if(key->get_keycode() == KEY_ENTER && screen == Screen::RESULTS) {
			reset_session();
			inp()->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
		} else if(key->get_keycode() == KEY_TAB) {
			if(screen == Screen::GAME) {
				return_screen = Screen::GAME;
				screen = Screen::SETTINGS;
				paused = true;
				sel_row = 0;
				inp()->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
				build_menu();
			} else {
				apply_action(ACT_BACK);
			}
		} else if(screen == Screen::SETTINGS && editing) {
			handle_edit_key(key->get_keycode());
		} else if(screen == Screen::SETTINGS) {
			if(key->get_keycode() == KEY_W || key->get_keycode() == KEY_UP) {
				sel_row = CLAMP(sel_row - 1, 0, buttons.size() - 1);
			} else if(key->get_keycode() == KEY_S || key->get_keycode() == KEY_DOWN) {
				sel_row = CLAMP(sel_row + 1, 0, buttons.size() - 1);
			} else if(key->get_keycode() == KEY_A || key->get_keycode() == KEY_LEFT) {
				adjust_selected(-1);
			} else if(key->get_keycode() == KEY_D || key->get_keycode() == KEY_RIGHT) {
				adjust_selected(1);
			} else if(key->get_keycode() == KEY_ENTER || key->get_keycode() == KEY_SPACE) {
				// Rows that take input can be typed into directly.
				if(sel_row >= 0 && sel_row < buttons.size() && row_is_editable(buttons[sel_row].action)) {
					editing = true;
					edit_buffer = "";
					edit_is_color = row_edits_color(buttons[sel_row].action);
				} else if(sel_row >= 0 && sel_row < buttons.size()) {
					apply_action(buttons[sel_row].action);
				}
			}
		}
	}
}

void AimTrainer::_unhandled_input(const Ref<InputEvent> &event) {
}

// True while this window has OS focus.
bool AimTrainer::is_holding_focus() const {
	Window *w = get_window();
	return w != nullptr && w->has_focus();
}

// Focus bookkeeping only. Mouse state is deliberately never polled here: the
// engine's polled button state does not observe events delivered through
// push_input, and a window that is simply not focused reports "button up"
// forever. Trusting it cancelled every in-progress charge (in automated runs and
// in any unfocused window), so the event stream is the single source of truth.
// A release that is genuinely swallowed while the window is unfocused costs at
// most one charge, and that is bounded: the target's own timer still expires it.
void AimTrainer::_notification(int p_what) {
	switch(p_what) {
		case NOTIFICATION_APPLICATION_FOCUS_OUT:
		case NOTIFICATION_WM_WINDOW_FOCUS_OUT:
			focus_lost = true;
			break;
		case NOTIFICATION_APPLICATION_FOCUS_IN:
		case NOTIFICATION_WM_WINDOW_FOCUS_IN:
			focus_lost = false;
			break;
		default:
			break;
	}
}

void AimTrainer::_process(double delta) {
	if(paused) {
		queue_redraw();
		return;
	}
	play_time += delta;

	if(screen == Screen::GAME) {
		update_targets(delta);
		update_hold(delta);
	}

	// Beam follow: the laser tracks the cursor whenever the field is live, and
	// reports which target it currently crosses so the reticle can lock on.
	update_laser_geometry();
	update_effects(delta);

	// Ambient motes drift across the field and wrap around.
	const Vector2 vp = get_viewport_rect().size;
	for(int i = 0; i < motes.size(); i++) {
		Mote &m = motes.ptrw()[i];
		m.phase += (float)delta * 1.4f;
		m.pos += m.vel * (float)delta;
		m.pos.x += sin(m.phase) * 8.0f * (float)delta;
		if(m.pos.x < -10.0f) { m.pos.x = vp.x + 10.0f; }
		if(m.pos.x > vp.x + 10.0f) { m.pos.x = -10.0f; }
		if(m.pos.y < -10.0f) { m.pos.y = vp.y + 10.0f; }
		if(m.pos.y > vp.y + 10.0f) { m.pos.y = -10.0f; }
	}

	// Debris: drifts outward, spins, then fades away.
	for(int i = debris.size() - 1; i >= 0; i--) {
		Debris &d = debris.ptrw()[i];
		d.age += (float)delta;
		if(d.age >= d.life) {
			debris.remove_at(i);
			continue;
		}
		d.pos += d.vel * (float)delta;
		d.vel *= 1.0f - 1.6f * (float)delta;
		d.angle += d.spin * (float)delta;
	}

	for(int i = pops.size() - 1; i >= 0; i--) {
		Pop &p = pops.ptrw()[i];
		p.age += (float)delta;
		if(p.age > 0.9f) {
			pops.remove_at(i);
		}
	}

	// Spawn pacing: one target at a time, with a generous gap so the field is
	// never overwhelmed. The gap narrows only late, alongside the lifetime ramp.
	const float ramp = CLAMP((play_time - tuning.ramp_start) / MAX(tuning.ramp_length, 1.0f), 0.0f, 1.0f);
	spawn_interval = tuning.spawn_interval + (tuning.spawn_interval_min - tuning.spawn_interval) * ramp;

	spawn_timer += delta;
	if(!spawn_suppressed && spawn_timer >= spawn_interval) {
		spawn_timer = 0.0f;
		int alive = 0;
		for(int i = 0; i < targets.size(); i++) {
			if(targets[i].active) {
				alive++;
			}
		}
		if(alive < tuning.target_count) {
			spawn_target();
		}
	}

	update_difficulty(delta);
	queue_redraw();
}

// 0 at the start of a session, 1 once the difficulty ramp is fully applied.
// The ramp runs across the whole session rather than topping out early: the
// first 80% takes `difficulty_ramp` seconds and the last stretch takes that long
// again, so difficulty keeps climbing instead of flat-lining at full strength.
float AimTrainer::difficulty_level() const {
	const float ramp = MAX(tuning.difficulty_ramp, 1.0f);
	if(session_time <= ramp) {
		return CLAMP((session_time / ramp) * 0.8f, 0.0f, 1.0f);
	}
	return CLAMP(0.8f + ((session_time - ramp) / ramp) * 0.2f, 0.0f, 1.0f);
}

// The charge a target spawned right now demands: it falls from the configured
// start-of-run hold down to hold_min as the ramp climbs, so later targets have
// to be released sooner and sooner.
float AimTrainer::current_hold_time() const {
	const float d = difficulty_level();
	return tuning.hold_initial + (tuning.hold_min - tuning.hold_initial) * d;
}

// The release window scales with the required hold, clamped so it never becomes
// either free or impossible: a 3 s hold gets 0.5 s, a 0.5 s hold gets 0.15 s.
// Deriving the fraction from the configured start-of-run pair (rather than
// recomputing it from the current hold) keeps the window exactly on the
// requested curve while the ramp is running.
float AimTrainer::current_hold_tolerance() const {
	const float span = MAX(tuning.hold_initial - tuning.hold_min, 0.0001f);
	const float progress = CLAMP((tuning.hold_initial - current_hold_time()) / span, 0.0f, 1.0f);
	const float start = MAX(tuning.hold_initial, tuning.hold_min) * tuning.hold_tolerance_frac;
	const float want = start + (tuning.hold_tolerance_min - start) * progress;
	return CLAMP(want, tuning.hold_tolerance_min, MAX(start, tuning.hold_tolerance_min));
}

void AimTrainer::update_difficulty(double delta) {
	session_time += (float)delta;

	// One time ramp drives the size cap down across its whole band, from
	// `max_size` to `min_size`. Letting accuracy shrink it as well (as this used
	// to) meant two mechanisms fighting over the same value: the cap stalled
	// partway and then never moved again.
	const float level = difficulty_level();
	const float target = tuning.max_size + (tuning.min_size - tuning.max_size) * level;
	current_max_size = CLAMP(target, tuning.min_size, tuning.max_size);
}

float AimTrainer::compute_distance_bonus(const Vector2 &p) const {
	const float d = p.length() / MAX(tuning.mouse_max_distance, 1.0f);
	return 1.0f + d * 1.5f;
}

float AimTrainer::compute_score(const Target &t) const {
	const float size_factor = 1.0f - ((t.radius - tuning.min_size) / MAX(tuning.max_size - tuning.min_size, 1.0f));
	float score = 100.0f * (0.5f + size_factor);
	score *= compute_distance_bonus(t.pos);
	if(t.gray) {
		score *= 1.5f;
	}
	if(t.moving) {
		score *= 1.25f;
	}
	return (int)score;
}

// The whole visible playfield is the spawn area. Targets used to live on a ring
// bounded by the beam's reach, but with the laser the aim only has to point at a
// target -- so there is no reason to keep them near the player point. What is
// left is a small margin (so a silhouette never pokes off the window) and a dead
// zone around the player point, where a target would be free to hit.
struct SpawnArea {
	Vector2 min;    // lower corner, DDNet units
	Vector2 max;    // upper corner, DDNet units
	float dead_zone; // targets must sit at least this far from the player point
};

static SpawnArea spawn_area_for(const Tuning &tuning, float px_per_unit, float radius) {
	// A margin in screen pixels, so it looks the same at every resolution.
	const float inset_px = 40.0f;
	const float inset = px_per_unit > 0.0001f ? inset_px / px_per_unit : inset_px;
	const float hx = MAX(tuning.field_width * 0.5f - inset, 10.0f);
	const float hy = MAX(tuning.field_height * 0.5f - inset, 10.0f);

	SpawnArea a;
	a.min = Vector2(-hx, -hy);
	a.max = Vector2(hx, hy);
	// Never larger than the shorter half-axis, or the dead zone would swallow
	// the whole area on a small field.
	a.dead_zone = MIN(MAX(tuning.mouse_max_distance * 0.12f, radius * 0.5f), hy * 0.5f);
	return a;
}

// Uniform point in the playfield, retried until it clears the dead zone. The
// retry keeps the distribution flat across the screen instead of pushing points
// outward into a ring, which is what the old radial spawn did.
static Vector2 sample_spawn_point(const SpawnArea &area, float dead_zone) {
	for(int attempt = 0; attempt < 24; attempt++) {
		const Vector2 p(area.min.x + (area.max.x - area.min.x) * ((float)rand() / (float)RAND_MAX),
				area.min.y + (area.max.y - area.min.y) * ((float)rand() / (float)RAND_MAX));
		if(p.length() >= dead_zone) {
			return p;
		}
	}
	// Degenerate field: fall back to the edge of the dead zone.
	const float ang = (float)rand() / (float)RAND_MAX * Math_TAU;
	return Vector2(cos(ang), sin(ang)) * dead_zone;
}

// Distance from the player point to the far corner of the playfield: the
// longest shot the beam ever has to make.
float AimTrainer::playfield_radius() const {
	const float hx = tuning.field_width * 0.5f;
	const float hy = tuning.field_height * 0.5f;
	return sqrtf(hx * hx + hy * hy);
}

void AimTrainer::spawn_target() {
	Target t;
	t.active = true;
	t.gray = timing_target_gray >= 0 ? timing_target_gray == 1 : ((float)rand() / (float)RAND_MAX < tuning.gray_chance);
	timing_target_gray = -1;
	t.shape = (rand() % 2) == 0 ? TargetShape::CIRCLE : TargetShape::SQUARE;
	const float max_r = current_max_size;
	t.radius = tuning.min_size + (max_r - tuning.min_size) * ((float)rand() / (float)RAND_MAX);
	// Charge time and its release window both come from the live difficulty.
	t.hold_time = current_hold_time();
	t.hold_tolerance = current_hold_tolerance();

	// Lifetime shrinks over time.
	const float t_factor = CLAMP((play_time - tuning.ramp_start) / tuning.ramp_length, 0.0f, 1.0f);
	t.life = tuning.base_lifetime + (tuning.min_lifetime - tuning.base_lifetime) * t_factor;

	// Anywhere on the visible field, minus a small margin and the dead zone
	// around the player point.
	const SpawnArea area = spawn_area_for(tuning, px_per_unit, t.radius);
	t.pos = sample_spawn_point(area, area.dead_zone);
	t.start_pos = t.pos;

	if((float)rand() / (float)RAND_MAX < tuning.moving_chance) {
		t.moving = true;
		t.move_duration = tuning.moving_duration_min + (tuning.moving_duration_max - tuning.moving_duration_min) * ((float)rand() / (float)RAND_MAX);
		t.move_speed = tuning.moving_speed_min + (tuning.moving_speed_max - tuning.moving_speed_min) * ((float)rand() / (float)RAND_MAX);
		t.move_dir = Vector2(cos((float)rand() / (float)RAND_MAX * Math_TAU), sin((float)rand() / (float)RAND_MAX * Math_TAU));
		t.ease = (MoveEase)(rand() % 6);
		t.retarget_chance_per_s = tuning.retarget_chance;
	}
	targets.push_back(t);
}

void AimTrainer::update_targets(double delta) {
	for(int i = 0; i < targets.size(); i++) {
		Target &t = targets.ptrw()[i];
		if(!t.active) {
			continue;
		}
		t.age += delta;

		// Keep inside the visible field: bounce off the margins instead of
		// clamping onto them, so a moving target never sticks to an edge.
		const SpawnArea area = spawn_area_for(tuning, px_per_unit, t.radius);
		const Vector2 lo = area.min + Vector2(t.radius, t.radius);
		const Vector2 hi = area.max - Vector2(t.radius, t.radius);

		if(t.moving && !freeze_motion) {
			t.move_time += delta;
			// Random retarget mid-flight: new random direction, possibly new ease.
			if(t.retarget_chance_per_s > 0.0f && (float)rand() / (float)RAND_MAX < t.retarget_chance_per_s * (float)delta) {
				t.start_pos = t.pos;
				t.move_time = 0.0f;
				t.move_duration = tuning.moving_duration_min + (tuning.moving_duration_max - tuning.moving_duration_min) * ((float)rand() / (float)RAND_MAX);
				t.move_speed = tuning.moving_speed_min + (tuning.moving_speed_max - tuning.moving_speed_min) * ((float)rand() / (float)RAND_MAX);
				t.move_dir = Vector2(cos((float)rand() / (float)RAND_MAX * Math_TAU), sin((float)rand() / (float)RAND_MAX * Math_TAU));
				t.ease = (MoveEase)(rand() % 6);
			}
			const float u = CLAMP(t.move_time / t.move_duration, 0.0f, 1.0f);
			const float e = ease_value(t.ease, u);
			t.pos = t.start_pos + t.move_dir * (t.move_speed * t.move_duration * e);

			// Keep inside the visible field: bounce off the margins instead of
			// clamping onto them, so a moving target never sticks to an edge.
			Vector2 bounced = Vector2();
			if(t.pos.x < lo.x) {
				t.pos.x = lo.x;
				bounced.x = 1.0f;
			} else if(t.pos.x > hi.x) {
				t.pos.x = hi.x;
				bounced.x = 1.0f;
			}
			if(t.pos.y < lo.y) {
				t.pos.y = lo.y;
				bounced.y = 1.0f;
			} else if(t.pos.y > hi.y) {
				t.pos.y = hi.y;
				bounced.y = 1.0f;
			}
			if(bounced.length_squared() > 0.0f) {
				t.move_dir = Vector2(bounced.x > 0.0f ? -t.move_dir.x : t.move_dir.x,
						bounced.y > 0.0f ? -t.move_dir.y : t.move_dir.y);
				if(t.move_dir.length_squared() < 0.0001f) {
					t.move_dir = Vector2(1, 0);
				}
				t.start_pos = t.pos;
				t.move_time = 0.0f;
			}
		}

		// A target being held is given extra time, because letting the timer run
		// out mid-charge would fail a run that was played correctly. The headroom
		// covers the longest charge the ramp can produce plus the widest window,
		// so it is never arbitrary -- and it is finite, and every live target is
		// subject to it, so nothing can be pinned on the field.
		const float held_limit = t.life + MAX(tuning.hold_initial, tuning.hold_min) +
				MAX(tuning.hold_initial, tuning.hold_min) * tuning.hold_tolerance_frac;
		if(t.age > (t.holding ? held_limit : t.life)) {
			if(t.holding) {
				t.holding = false;
			}
			if(holding_index == i) {
				holding_index = -1;
				hold_scored = false;
			}
			stats.record_interaction(false);
			add_miss_record(to_screen(t.pos));
			add_pop(to_screen(t.pos), true);
			add_ring(to_screen(t.pos), Color(0.98f, 0.42f, 0.35f), t.radius * px_per_unit, t.radius * px_per_unit * 2.4f, 0.5f);
			add_hit_marker(to_screen(t.pos), true);
			clear_target(t, true);
		}
	}

	// Compact the list occasionally.
	int alive = 0;
	for(int i = 0; i < targets.size(); i++) {
		if(targets[i].active) {
			alive++;
		}
	}
	if(alive == 0 && targets.size() > 0) {
		targets.clear();
	}
}

// Radius of the silhouette actually drawn for a target, in screen pixels. The
// renderer insets the painted shape slightly inside `radius` (so the rim and the
// life ring have room), and the hitbox has to follow: using the raw radius made
// the beam miss a target whose painted edge it was visibly crossing.
static float target_draw_radius(const Target &t, float px_per_unit) {
	return t.radius * px_per_unit * (t.shape == TargetShape::CIRCLE ? 0.96f : 0.94f);
}

// Closest silhouette the beam crosses. The beam leaves the player point at the
// screen centre along `dir` (the direction from the centre to the cursor) and
// runs to the far edge of the viewport, so the cursor is only a direction
// indicator: what counts is what the beam actually passes through.
Target *AimTrainer::find_laser_target(const Vector2 &dir, Vector2 &hit_point, float &lateral) {
	Target *best = nullptr;
	float best_t = 1e30f;
	float best_lat = 1e30f;
	hit_point = screen_center;

	if(dir.length_squared() < 1e-8f) {
		lateral = 0.0f;
		return nullptr;
	}
	const Vector2 d = dir.normalized();

	for(int i = 0; i < targets.size(); i++) {
		Target &t = targets.ptrw()[i];
		if(!t.active) {
			continue;
		}
		// Everything below is in screen pixels.
		const Vector2 world = to_screen(t.pos);
		const float r_draw = target_draw_radius(t, px_per_unit);
		// The beam is a thick line, i.e. a capsule of half-width `laser_width`,
		// and the silhouette is approximated by a disc of the drawn radius (for
		// squares, half the diagonal, which is a touch forgiving at the corners).
		// A hit is "capsule overlaps disc", so a beam that visibly touches the
		// shape connects instead of only a beam that happens to pass near the
		// centre.
		const float along = MAX((world - screen_center).dot(d), 0.0f);
		const Vector2 closest = screen_center + d * along;
		const float lat = (world - closest).length();
		// A small tolerance covers the antialiased edge and the life ring, so the
		// hitbox is never smaller than what the player can see.
		const float reach = MAX(tuning.laser_width, 2.0f) + r_draw + 1.5f;
		if(lat > reach) {
			continue;
		}
		// Ordering: the silhouette nearest the player point wins. Back the impact
		// point off to where the beam meets the shape.
		const float flat = MIN(lat, r_draw);
		const float entry = MAX(0.0f, along - sqrtf(MAX(r_draw * r_draw - flat * flat, 0.0f)));
		if(entry < best_t - 0.0001f || (std::fabs(entry - best_t) < 0.0001f && lat < best_lat)) {
			best_t = entry;
			best_lat = lat;
			best = &t;
			hit_point = screen_center + d * entry;
		}
	}
	lateral = best ? best_lat : 0.0f;
	return best;
}


Target *AimTrainer::find_nearest(const Vector2 &p, float &distance) {
	Target *best = nullptr;
	distance = 1e9f;
	for(int i = 0; i < targets.size(); i++) {
		if(!targets[i].active) {
			continue;
		}
		const float d = targets[i].point_in_distance(p);
		if(d < distance) {
			distance = d;
			best = &targets.ptrw()[i];
		}
	}
	return best;
}

void AimTrainer::add_pop(const Vector2 &p, bool miss) {
	add_pop(p, miss, 0);
}

void AimTrainer::add_pop(const Vector2 &p, bool miss, int score) {
	Pop pop;
	pop.pos = p;
	pop.miss = miss;
	pop.score = score;
	pops.push_back(pop);
	// Keep the effect lists bounded even after a long session.
	if(pops.size() > 64) {
		pops.remove_at(0);
	}
}

void AimTrainer::add_hit_flash(const Vector2 &p, float radius, bool gray) {
	HitFlash f;
	f.pos = p;
	f.radius = radius;
	f.gray = gray;
	f.life = 0.32f;
	hit_flashes.push_back(f);
	if(hit_flashes.size() > 48) {
		hit_flashes.remove_at(0);
	}
}

void AimTrainer::add_ring(const Vector2 &p, const Color &col, float from_r, float to_r, float life) {
	Ring r;
	r.pos = p;
	r.col = col;
	r.from_r = from_r;
	r.to_r = to_r;
	r.life = MAX(life, 0.05f);
	rings.push_back(r);
	if(rings.size() > 48) {
		rings.remove_at(0);
	}
}

void AimTrainer::add_hit_marker(const Vector2 &p, bool miss) {
	HitMarker h;
	h.pos = p;
	h.miss = miss;
	h.vel = Vector2(((float)rand() / (float)RAND_MAX - 0.5f) * 60.0f,
			-40.0f - (float)rand() / (float)RAND_MAX * 50.0f);
	hit_markers.push_back(h);
	if(hit_markers.size() > 48) {
		hit_markers.remove_at(0);
	}
}

// Sparks ejected along the beam: they leave the muzzle and streak toward the
// impact, which sells the beam as a physical shot rather than a drawn line.
void AimTrainer::spawn_sparks(const Vector2 &p, int count) {
	for(int i = 0; i < count; i++) {
		Spark s;
		s.pos = screen_center + (p - screen_center) * ((float)rand() / (float)RAND_MAX) * 0.85f;
		s.speed = 260.0f + (float)rand() / (float)RAND_MAX * 520.0f;
		s.offset = ((float)rand() / (float)RAND_MAX - 0.5f) * 14.0f;
		s.size = 1.0f + (float)rand() / (float)RAND_MAX * 2.2f;
		s.life = 0.16f + (float)rand() / (float)RAND_MAX * 0.22f;
		sparks.push_back(s);
	}
	if(sparks.size() > 160) {
		sparks.remove_at(0);
	}
}

// The beam always leaves the player point and runs through the cursor. When it
// crosses a silhouette it stops on that silhouette's near edge, so the impact
// flare sits on the target; otherwise it carries on well past the cursor, which
// is what makes it read as a ray rather than a leash.
void AimTrainer::update_laser_geometry() {
	if(input.pos.length_squared() > 1e-8f) {
		laser_dir = input.pos.normalized();
	}
	const float cursor_dist = input.pos.length() * px_per_unit;
	// Free beam length. It has to cover the whole playfield, because targets now
	// spawn across all of it, plus a margin so the beam visibly runs off-screen.
	const float free_reach = MAX(cursor_dist + 480.0f, (playfield_radius() + 120.0f) * px_per_unit);

	if(screen != Screen::GAME || paused) {
		laser_lock = false;
		laser_reach = free_reach;
		laser_end = screen_center + laser_dir * laser_reach;
		return;
	}

	Vector2 hit_point;
	float lateral = 0.0f;
	Target *t = find_laser_target(laser_dir, hit_point, lateral);
	laser_lock = t != nullptr;
	if(laser_lock) {
		laser_reach = MIN(MAX(48.0f, (hit_point - screen_center).length() + 4.0f), laser_max_reach());
	} else {
		laser_reach = MIN(free_reach, laser_max_reach());
	}
	laser_end = screen_center + laser_dir * laser_reach;
}

// How far the beam may be drawn: far enough to leave the window in any
// direction, and never less than the playfield's own diagonal so no target can
// sit out of the beam's reach.
float AimTrainer::laser_max_reach() const {
	const Vector2 vs = get_viewport_rect().size;
	const float half = Vector2(MAX(vs.x, 1.0f), MAX(vs.y, 1.0f)).length() * 0.5f;
	return MAX(half + 80.0f, (playfield_radius() + 120.0f) * px_per_unit);
}

void AimTrainer::update_effects(double delta) {
	const float d = (float)delta;
	effects_clock += d;

	// Fire flash decays quickly; it drives the muzzle bloom and beam width.
	fire_flash = MAX(0.0f, fire_flash - d * 5.0f);

	// Screen shake.
	shake_amount = MAX(0.0f, shake_amount - d * 34.0f);
	if(shake_amount > 0.001f) {
		shake_offset = Vector2(sin(effects_clock * 71.0f), cos(effects_clock * 83.0f)) * shake_amount;
	} else {
		shake_offset = Vector2();
		shake_amount = 0.0f;
	}

	for(int i = hit_flashes.size() - 1; i >= 0; i--) {
		hit_flashes.ptrw()[i].age += d;
		if(hit_flashes.ptrw()[i].age >= hit_flashes.ptrw()[i].life) {
			hit_flashes.remove_at(i);
		}
	}
	for(int i = rings.size() - 1; i >= 0; i--) {
		rings.ptrw()[i].age += d;
		if(rings.ptrw()[i].age >= rings.ptrw()[i].life) {
			rings.remove_at(i);
		}
	}
	for(int i = hit_markers.size() - 1; i >= 0; i--) {
		HitMarker &h = hit_markers.ptrw()[i];
		h.age += d;
		h.pos += h.vel * d;
		h.vel *= 1.0f - 2.4f * d;
		if(h.age >= h.life) {
			hit_markers.remove_at(i);
		}
	}
	for(int i = sparks.size() - 1; i >= 0; i--) {
		Spark &s = sparks.ptrw()[i];
		s.age += d;
		if(s.age >= s.life) {
			sparks.remove_at(i);
			continue;
		}
		const Vector2 dir = (laser_end - screen_center);
		const float len = MAX(dir.length(), 0.0001f);
		const Vector2 axis = dir / len;
		const Vector2 perp = Vector2(-axis.y, axis.x);
		s.pos += axis * s.speed * d + perp * s.offset * 4.0f * d;
		if((s.pos - screen_center).length() > len + 40.0f) {
			sparks.remove_at(i);
		}
	}
}

void AimTrainer::add_miss_record(const Vector2 &cursor) {
	float nearest = 0.0f;
	Target *t = find_nearest(cursor, nearest);
	MissRecord m;
	m.nearest_distance = nearest;
	m.error_vector = cursor;
	if(t) {
		m.had_target_nearby = true;
		m.target_radius = t->radius;
		const float gap = MAX(0.0f, nearest - t->radius);
		m.deviation_pct = t->radius > 0.0f ? (gap / t->radius) * 100.0f : 0.0f;
		const Vector2 to_target = t->pos - cursor;
		m.overshoot = to_target.dot(cursor - t->pos) < 0.0f;
		m.error_vector = to_target;
	}
	stats.record_miss(m);
}

void AimTrainer::score_target(Target &t, bool success, const Vector2 &cursor) {
	if(success) {
		const int s = compute_score(t);
		stats.score_total += s;
		if(t.gray) {
			stats.score_gray += s;
			stats.hits_gray++;
		} else {
			stats.score_white += s;
			stats.hits_white++;
		}
		stats.record_interaction(true);
		last_score = s;
		add_pop(to_screen(t.pos), false, s);
	} else {
		stats.record_interaction(false);
		add_miss_record(cursor);
		add_pop(to_screen(t.pos), true);
	}
	clear_target(t, true);
	t.active = false;
	t.holding = false;
	for(int i = 0; i < targets.size(); i++) {
		if(&targets.ptrw()[i] == &t) {
			holding_index = -1;
			hold_scored = false;
			break;
		}
	}
}

// Fires the beam. The cursor is a direction only: the shot resolves against the
// closest silhouette the beam crosses (the one nearest the player point), which
// is what "попадание по ближней фигуре на лазере" means.
void AimTrainer::resolve_mouse_click(const Vector2 &p, bool left_button) {
	if(paused || screen != Screen::GAME) {
		return;
	}
	// A left click never resolves during a charge (belt and braces: _input
	// already drops it, and the debug hook comes through here directly).
	if(left_button && (rmb_held || holding_index >= 0)) {
		return;
	}

	// Muzzle flash + a little kick so every shot reads as an event.
	fire_flash = 1.0f;
	shake_amount = MAX(shake_amount, left_button ? 3.2f : 2.2f);

	Vector2 hit_point;
	float lateral = 0.0f;
	const Vector2 dir = p.length_squared() > 1e-8f ? p.normalized() : Vector2(1, 0);
	Target *t = find_laser_target(dir, hit_point, lateral);

	// Every trigger pull is logged, whatever it hits.
	stats.shot_times.push_back(play_time - session_origin);
	last_interaction_time = play_time;

	if(t && left_button && !t->gray) {
		score_target(*t, true, p);
		return;
	}
	if(t && !left_button && t->gray) {
		// Hold start: the beam decides what is under the aim. The press already
		// counts as a successful hit (it was on target); the hold itself then
		// pays out on release inside the tolerance window.
		on_hold_started(*t);
		hold_scored = true;
		add_hit_flash(to_screen(t->pos), t->radius * px_per_unit, t->gray);
		stats.record_interaction(true);
		return;
	}

	if(t) {
		// A real target was on the beam but the shot could not use it: LMB on a
		// gray target or RMB on a white one. That is a genuine accuracy miss and
		// nothing more -- the target stays fully interactive, so the correct
		// button can still resolve it afterwards. Freezing it out would punish
		// one slip twice.
		stats.record_interaction(false);
		add_miss_record(p);
		add_pop(p, true);
		add_ring(p, Color(0.98f, 0.32f, 0.30f), 6.0f, 46.0f, 0.42f);
		add_hit_marker(p, true);
		spawn_sparks(p, 6);
		return;
	}

	// Nothing on the beam: the shot went into open space. Accuracy counts every
	// shot, so this is a plain miss -- spraying costs accuracy like any other
	// wasted trigger pull. The record is tagged with NO_TARGET so the results
	// screen can label it as a bare shot rather than an aiming error.
	MissRecord empty;
	empty.nearest_distance = MissRecord::NO_TARGET;
	empty.target_radius = MissRecord::NO_TARGET;
	empty.deviation_pct = MissRecord::NO_TARGET;
	empty.had_target_nearby = false;
	empty.error_vector = Vector2();
	stats.record_interaction(false, &empty);
	add_pop(p, true);
	add_ring(p, Color(0.98f, 0.32f, 0.30f), 6.0f, 46.0f, 0.42f);
	add_hit_marker(p, true);
	spawn_sparks(p, 6);
}

void AimTrainer::on_hold_started(Target &t) {
	t.holding = true;
	t.hold_progress = 0.0f;
	t.hold_elapsed = 0.0f;
	for(int i = 0; i < targets.size(); i++) {
		if(&targets.ptrw()[i] == &t) {
			holding_index = i;
			break;
		}
	}
}

// Drops an active charge without scoring or billing anything: the target goes
// back to being an ordinary live target that can be charged again or will
// simply time out. Used when a hold is found to be unsatisfiable (RMB is not
// actually down any more), which is what used to leave a target pinned forever.
void AimTrainer::abandon_hold() {
	if(holding_index >= 0 && holding_index < targets.size()) {
		Target &t = targets.ptrw()[holding_index];
		t.holding = false;
		t.hold_progress = 0.0f;
		t.hold_elapsed = 0.0f;
	}
	holding_index = -1;
	hold_scored = false;
}

void AimTrainer::update_hold(double delta) {
	if(holding_index < 0 || holding_index >= targets.size()) {
		holding_index = -1;
		hold_scored = false;
		return;
	}
	Target &t = targets.ptrw()[holding_index];
	if(!t.active || !t.gray) {
		holding_index = -1;
		hold_scored = false;
		return;
	}
	if(t.holding) {
		t.hold_elapsed += delta;
		t.hold_progress = CLAMP(t.hold_elapsed / MAX(t.hold_time, 0.0001f), 0.0f, 1.0f);
		// Reaching the required duration only arms completion: the hold still
		// finishes on RMB release, even if the cursor has left the target.
	}
}

void AimTrainer::on_hold_released(bool released_by_timeout) {
	(void)released_by_timeout; // both the event and the poll path land here
	if(holding_index < 0 || holding_index >= targets.size()) {
		holding_index = -1;
		return;
	}
	Target &t = targets.ptrw()[holding_index];
	if(!t.active || !t.holding) {
		// Both the release event and the per-frame poll can land here; the
		// second one must not score or clear the target a second time.
		holding_index = -1;
		return;
	}
	const float required = t.hold_time;
	const float actual = t.hold_elapsed;
	HoldDeviation d;
	d.required = required;
	d.actual = actual;
	d.deviation = actual - required;

	stats.shot_times.push_back(play_time - session_origin);

	// Tolerance window: releasing within +/- tolerance around the required
	// duration counts as a success; otherwise it is an early/late release miss.
	// The window travelled with the target, so it shrinks alongside the ramp.
	const bool within_window = std::fabs(d.deviation) <= t.hold_tolerance;
	if(within_window) {
		stats.record_hold(d);
		const int s = compute_score(t);
		stats.score_total += s;
		stats.score_gray += s;
		if(!hold_scored) {
			// The press itself already counted as a hit; only a press that was
			// not on target (or a hold that began outside the beam) bills here.
			stats.hits_gray++;
			stats.record_interaction(true);
		}
		last_score = s;
		add_pop(to_screen(t.pos), false, s);
		add_hit_flash(to_screen(t.pos), t.radius * px_per_unit, true);
		add_ring(to_screen(t.pos), Color(0.45f, 1.0f, 0.62f), t.radius * px_per_unit * 0.8f, t.radius * px_per_unit * 2.6f, 0.5f);
		shake_amount = MAX(shake_amount, 5.5f);
	} else {
		stats.record_interaction(false);
		add_miss_record(input.pos);
		add_pop(to_screen(t.pos), true);
		add_hit_marker(to_screen(t.pos), true);
		add_ring(to_screen(t.pos), Color(0.98f, 0.42f, 0.35f), t.radius * px_per_unit, t.radius * px_per_unit * 2.4f, 0.5f);
	}
	clear_target(t, true);
	t.holding = false;
	t.active = false;
	holding_index = -1;
	hold_scored = false;
	last_interaction_time = play_time;
}

// ---- Test/debug hooks -------------------------------------------------------

int AimTrainer::get_active_target_count() const {
	int n = 0;
	for(int i = 0; i < targets.size(); i++) {
		if(targets[i].active) {
			n++;
		}
	}
	return n;
}

int AimTrainer::get_hold_duration_ms() {
	if(holding_index < 0 || holding_index >= (int)targets.size()) {
		return -1;
	}
	return (int)roundf(targets.ptrw()[holding_index].hold_time * 1000.0f);
}

float AimTrainer::get_hold_progress() {
	if(holding_index < 0 || holding_index >= (int)targets.size()) {
		return -1.0f;
	}
	return targets.ptrw()[holding_index].hold_progress;
}

float AimTrainer::debug_holding_duration_s() const {
	if(holding_index < 0 || holding_index >= (int)targets.size()) {
		return -1.0f;
	}
	return targets[holding_index].hold_time;
}

Dictionary AimTrainer::debug_tuning() const {
	Dictionary d;
	d["target_count"] = tuning.target_count;
	d["max_size"] = tuning.max_size;
	d["min_size"] = tuning.min_size;
	d["spawn_interval"] = tuning.spawn_interval;
	d["mouse_max_distance"] = tuning.mouse_max_distance;
	d["mouse_sens"] = tuning.mouse_sens;
	d["laser_width"] = tuning.laser_width;
	d["playfield_radius"] = playfield_radius();
	d["dead_zone"] = spawn_area_for(tuning, px_per_unit, tuning.min_size).dead_zone;
	d["current_max_size"] = current_max_size;
	d["difficulty"] = difficulty_level();
	d["session_time"] = session_time;
	d["hold_now"] = current_hold_time();
	d["hold_tol_now"] = current_hold_tolerance();
	d["hold_initial"] = tuning.hold_initial;
	d["hold_min"] = tuning.hold_min;
	d["hold_tol_frac"] = tuning.hold_tolerance_frac;
	d["gray_chance"] = tuning.gray_chance;
	d["color_gray"] = tuning.color_gray;
	d["color_white"] = tuning.color_white;
	d["fps"] = Engine::get_singleton()->get_max_fps();
	return d;
}

PackedStringArray AimTrainer::debug_config_keys() const {
	PackedStringArray out;
	Ref<ConfigFile> cf;
	cf.instantiate();
	if(cf->load("user://aimtrainer.cfg") != OK) {
		return out;
	}
	// Flatten section/key pairs into "section/key" so a test can check presence.
	const PackedStringArray sections = cf->get_sections();
	for(int i = 0; i < sections.size(); i++) {
		const PackedStringArray keys = cf->get_section_keys(sections[i]);
		for(int k = 0; k < keys.size(); k++) {
			out.push_back(String(sections[i]) + "/" + String(keys[k]));
		}
	}
	// Sort for a stable comparison.
	for(int i = 0; i < out.size(); i++) {
		for(int j = i + 1; j < out.size(); j++) {
			if(String(out[j]) < String(out[i])) {
				const String tmp = out[i];
				out.set(i, out[j]);
				out.set(j, tmp);
			}
		}
	}
	return out;
}

PackedStringArray AimTrainer::debug_menu_rows() const {
	PackedStringArray out;
	for(int i = 0; i < buttons.size(); i++) {
		out.push_back(buttons[i].label);
	}
	return out;
}

float AimTrainer::debug_farthest_target() const {
	float far = 0.0f;
	for(int i = 0; i < targets.size(); i++) {
		if(targets[i].active) {
			far = MAX(far, targets[i].pos.length());
		}
	}
	return far;
}

float AimTrainer::debug_spawn_reach() const {
	// The farthest a spawn point can sit from the player point.
	const SpawnArea area = spawn_area_for(tuning, px_per_unit, tuning.min_size);
	return MAX(area.max.length(), area.min.length());
}

bool AimTrainer::debug_any_target_unreachable() const {
	// "Unreachable" now means outside the visible playfield, i.e. a target the
	// player could not see or point at.
	const float hx = tuning.field_width * 0.5f;
	const float hy = tuning.field_height * 0.5f;
	for(int i = 0; i < targets.size(); i++) {
		if(!targets[i].active) {
			continue;
		}
		const Vector2 p = targets[i].pos;
		if(std::fabs(p.x) > hx || std::fabs(p.y) > hy) {
			return true;
		}
	}
	return false;
}

// Test hook: drops the active charge exactly like a stranded release would.
void AimTrainer::debug_break_hold() {
	rmb_held = false;
	abandon_hold();
}

PackedStringArray AimTrainer::debug_target_dump() const {
	PackedStringArray out;
	for(int i = 0; i < targets.size(); i++) {
		const Target &t = targets[i];
		out.push_back("idx=" + String::num(i) + " active=" + String::num(t.active ? 1 : 0) +
				" holding=" + String::num(t.holding ? 1 : 0) +
				" age=" + String::num(t.age, 2) + " life=" + String::num(t.life, 2) +
				" hold=" + String::num(t.hold_time, 2) + " tol=" + String::num(t.hold_tolerance, 2));
	}
	out.push_back("holding_index=" + String::num(holding_index) +
			" hold_scored=" + String::num(hold_scored ? 1 : 0) +
			" rmb_held=" + String::num(rmb_held ? 1 : 0) +
			" paused=" + String::num(paused ? 1 : 0) +
			" screen=" + String::num((int)screen));
	return out;
}

// Test fixture helper: sets a tuning value directly instead of walking the menu.
void AimTrainer::debug_set_tuning(const String &key, double value) {
	const float v = (float)value;
	if(key == "target_count") {
		tuning.target_count = CLAMP((int)value, 1, 12);
	} else if(key == "max_size") {
		tuning.max_size = CLAMP(v, tuning.min_size, 200.0f);
	} else if(key == "spawn_interval") {
		tuning.spawn_interval = CLAMP(v, 0.20f, 5.0f);
		tuning.spawn_interval_min = MIN(tuning.spawn_interval_min, tuning.spawn_interval);
	} else if(key == "mouse_max_distance") {
		tuning.mouse_max_distance = CLAMP(v, 50.0f, 2000.0f);
		sync_input_from_tuning();
	} else if(key == "hold_initial") {
		tuning.hold_initial = CLAMP(v, tuning.hold_min, 10.0f);
	} else if(key == "hold_min") {
		tuning.hold_min = CLAMP(v, 0.10f, tuning.hold_initial);
	} else if(key == "gray_chance") {
		tuning.gray_chance = CLAMP(v, 0.0f, 1.0f);
	}
	apply_tuning_changes();
}

void AimTrainer::debug_set_color(const String &which, const Color &c) {
	Color v = c;
	v.a = 1.0f;
	if(which == "gray") {
		tuning.color_gray = v;
	} else if(which == "white") {
		tuning.color_white = v;
	}
}

Rect2 AimTrainer::debug_spawn_area() const {
	const SpawnArea area = spawn_area_for(tuning, px_per_unit, tuning.min_size);
	return Rect2(area.min, area.max - area.min);
}

void AimTrainer::debug_simulate_focus_loss(bool lost) {
	focus_lost = lost;
}

// Mirrors the per-frame cursor setup in _input so tests exercise the same
// DDNet clamp path the real game uses.
void AimTrainer::sync_input_from_tuning() {
	input.mouse_sens = tuning.mouse_sens;
	input.max_distance = tuning.mouse_max_distance;
	input.follow_factor = 0.0f;
	input.deadzone = 0.0f;
	input.min_distance = 0.0f;
}

void AimTrainer::debug_force_move_cursor(const Vector2 &delta) {
	sync_input_from_tuning();
	(void)input.apply_delta(delta);
}

// Test hook: forces the cursor exactly onto a DDNet-space point, bypassing the
// incremental delta path so a test can aim at a target in one step.
void AimTrainer::debug_set_cursor(const Vector2 &pos) {
	sync_input_from_tuning();
	input.pos = pos;
	input.clamp_mouse_pos();
	update_laser_geometry();
}

// Test hook: does the beam currently cross any live target?
bool AimTrainer::debug_laser_has_target() {
	Vector2 hp;
	float lat = 0.0f;
	return find_laser_target(laser_dir, hp, lat) != nullptr;
}

// Clears the field and spawns exactly one target of the requested colour,
// returning true on success. The test then presses/releases the real input
// events through Input, so the production code path is what gets measured.
bool AimTrainer::debug_prepare(bool want_gray) {
	for(int i = 0; i < targets.size(); i++) {
		clear_target(targets.ptrw()[i], false);
	}
	timing_target_gray = want_gray;
	spawn_target();
	for(int i = 0; i < 12; i++) {
		bool has = false;
		for(int j = 0; j < targets.size(); j++) {
			if(targets[j].active) {
				has = true;
				break;
			}
		}
		if(has && first_target_is_gray() == want_gray) {
			return true;
		}
		spawn_target();
	}
	return first_target_is_gray() == want_gray;
}

Vector2 AimTrainer::first_target_pos() const {
	for(int i = 0; i < targets.size(); i++) {
		if(targets[i].active) {
			return targets[i].pos;
		}
	}
	return Vector2();
}

bool AimTrainer::first_target_is_gray() const {
	for(int i = 0; i < targets.size(); i++) {
		if(targets[i].active) {
			return targets[i].gray;
		}
	}
	return false;
}

float AimTrainer::first_target_radius() const {
	for(int i = 0; i < targets.size(); i++) {
		if(targets[i].active) {
			return targets[i].radius;
		}
	}
	return -1.0f;
}
