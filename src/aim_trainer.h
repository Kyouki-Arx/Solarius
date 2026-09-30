#pragma once

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/templates/vector.hpp>

#include "ddnet_input.h"
#include "session_stats.h"
#include "target.h"

namespace godot {

enum class Screen { GAME, RESULTS, SETTINGS };

// Settings-menu row actions.
enum MenuAction {
	ACT_FPS = 1,
	ACT_RES,
	ACT_MODE,
	ACT_SENS,
	ACT_MAXDIST,
	ACT_TARGETS,
	ACT_MAXSIZE,
	ACT_SPAWN,
	ACT_HOLD,
	ACT_HOLD_MIN,
	ACT_TOLERANCE,
	ACT_COLOR_GRAY,
	ACT_COLOR_WHITE,
	ACT_GRAY_CHANCE,
	ACT_BACK,
};

Input *inp();

struct Tuning {
	// Spawn sizes (half-extent / radius in pixels).
	float min_size = 22.0f;
	float max_size = 46.0f;
	// Number of targets alive at once.
	//
	// Pacing is deliberately generous: every target gets time to be acquired,
	// tracked along the beam and resolved, so a perfectly played run can hold
	// 100% accuracy instead of drowning in simultaneous lifetimes.
	int target_count = 3;
	// Time to hit a target before it times out (seconds).
	float base_lifetime = 9.0f;
	float min_lifetime = 4.5f;
	// Lifetime starts to shrink only after this much play time (seconds).
	float ramp_start = 45.0f;
	float ramp_length = 240.0f;
	// Difficulty ramp, driven by session time. It reaches 80% after this many
	// seconds and the remaining 20% after as many again, so difficulty keeps
	// climbing for the whole session. Two things get harder as it climbs:
	//   * the maximum spawn size shrinks from max_size all the way to min_size,
	//   * the required hold time shrinks from hold_initial down to hold_min.
	float difficulty_ramp = 150.0f;
	// Hold time window: the charge demanded at the start of a run, and the floor
	// it ramps down to.
	float hold_min = 0.5f;
	float hold_initial = 3.0f;
	// Release window at the start of a run, as a fraction of the start-of-run
	// hold. It tightens with the ramp, never below `hold_tolerance_min`.
	float hold_tolerance_frac = 0.25f;
	float hold_tolerance_min = 0.15f;
	// Chance (0..1) that one target is asked to be gray (RMB hold) rather than
	// white (LMB). Sanity-clamped to 0..1 on load.
	float gray_chance = 0.45f;
	// Target colours. White targets take `color_white`, gray ones `color_gray`;
	// everything else is derived from the target's own colour, so custom colours
	// stay consistent across the flash, rim and glow.
	Color color_gray = Color(0.55f, 0.55f, 0.58f);
	Color color_white = Color(0.97f, 0.97f, 0.98f);
	// Chance per target of being a moving one.
	float moving_chance = 0.18f;
	float moving_speed_min = 140.0f;
	float moving_speed_max = 420.0f;
	float moving_duration_min = 1.0f;
	float moving_duration_max = 3.0f;
	// Retarget mid-flight probability per second.
	float retarget_chance = 0.30f;
	// Gap between spawns (seconds) and the minimum gap allowed once ramping.
	float spawn_interval = 1.10f;
	float spawn_interval_min = 0.55f;
	// Half-width of the laser's hit corridor, in screen pixels. This is the
	// capsule radius used for hit detection, so it must be at least as wide as
	// the beam looks: a beam visibly touching a silhouette has to count.
	float laser_width = 12.0f;
	// Cursor circle radius around player (DDNet units).
	// DDNet default is cl_mouse_max_distance 400, with cl_mouse_followfactor 0
	// and cl_mouse_deadzone 0, so the effective max is the raw max distance.
	float mouse_max_distance = 400.0f;
	// inp_mousesens default (200 = 1.0 in DDNet).
	float mouse_sens = 200.0f;
	// Playfield in DDNet units, sized so the cursor circle fills the window.
	float field_width = 1000.0f;
	float field_height = 720.0f;
};

class AimTrainer : public Node2D {
	GDCLASS(AimTrainer, Node2D)

public:
	AimTrainer();
	~AimTrainer() {}

	void _ready() override;
	void _process(double delta) override;
	void _input(const Ref<InputEvent> &event) override;
	void _draw() override;
	void _unhandled_input(const Ref<InputEvent> &event) override;
	void _notification(int p_what);

	void reset_session();

	// Test/debug hooks (harmless in normal play).
	// Test/debug hooks (harmless in normal play).
	int get_target_count() const { return (int)targets.size(); }
	int get_active_target_count() const;
	Vector2 get_cursor_pos() const { return input.pos; }
	int get_score() const { return stats.score_total; }
	int get_miss_count() const { return stats.misses; }
	int get_hit_count() const { return stats.hits; }
	// Shots that had nothing on the beam at all (a subset of the misses).
	int get_bare_shot_count() const { return stats.bare_shots; }
	int get_shot_count() const { return stats.total_interactions; }
	float get_accuracy() const { return stats.current_accuracy; }
	float get_play_time() const { return play_time; }
	int get_hold_duration_ms();
	float get_hold_progress();
	Vector2 first_target_pos() const;
	bool first_target_is_gray() const;
	float first_target_radius() const;
	void debug_force_move_cursor(const Vector2 &delta);
	void debug_spawn_target() { spawn_target(); }
	bool debug_prepare(bool want_gray);
	void debug_open_settings() { screen = Screen::SETTINGS; paused = true; build_menu(); queue_redraw(); }
	void debug_open_results() { screen = Screen::RESULTS; paused = true; build_menu(); queue_redraw(); }
	void debug_click(const Vector2 &ddnet_pos, bool left) { resolve_mouse_click(ddnet_pos, left); }
	float get_sens() const { return tuning.mouse_sens; }
	float get_maxdist() const { return tuning.mouse_max_distance; }
	bool is_laser_locked() const { return laser_lock; }
	// Test hooks: what the beam is currently doing (DDNet units / pixels).
	Vector2 laser_tip_dir() const { return to_ddnet(laser_end); }
	float laser_reach_px() const { return laser_reach; }
	bool debug_laser_has_target();
	// Forced aim for tests: drives the DDNet cursor straight to `pos`.
	void debug_set_cursor(const Vector2 &pos);
	// Clears the field and spawns exactly one target of the requested colour.
	void debug_spawn_color(bool gray) { timing_target_gray = gray ? 1 : 0; spawn_target(); }
	void debug_clear_targets() { targets.clear(); }
	bool debug_is_gray() const { return first_target_is_gray(); }
	int debug_active() const { return get_active_target_count(); }
	int debug_first_hold_ms() { return get_hold_duration_ms(); }
	// Freezes / re-enables target motion so tests are deterministic.
	void debug_freeze_motion(bool frozen) { freeze_motion = frozen; }
	// Stops / restarts the automatic top-up spawns.
	void debug_suppress_spawn(bool on) { spawn_suppressed = on; }
	// Sets a tuning value directly (no menu round-trip), for test fixtures.
	void debug_set_tuning(const String &key, double value);
	// Bounding box of the spawn area in DDNet units: x, y, width, height.
	Rect2 debug_spawn_area() const;
	// Simulates losing window focus, which is when the polled button state takes
	// over as the authority on RMB.
	void debug_simulate_focus_loss(bool lost);
	// Jumps the session clock, so the difficulty ramp can be tested directly.
	void debug_set_session_time(double seconds) { session_time = (float)MAX(seconds, 0.0); }
	float debug_difficulty_level() const { return difficulty_level(); }
	// Sets a target colour directly ("gray" / "white").
	void debug_set_color(const String &which, const Color &c);
	// Hold duration of the target currently being held, or -1 when idle.
	float debug_holding_duration_s() const;
	// Current tuning values, so tests can assert on settings.
	Dictionary debug_tuning() const;
	// Sorted list of the config keys actually present in user://aimtrainer.cfg.
	PackedStringArray debug_config_keys() const;
	// Menu labels in row order.
	PackedStringArray debug_menu_rows() const;
	// Menu navigation/state for tests.
	void debug_menu_select(int index) { sel_row = CLAMP(index, 0, MAX(0, buttons.size() - 1)); queue_redraw(); }
	void debug_menu_adjust(int dir) { adjust_selected(dir); queue_redraw(); }
	void debug_close_menu() { apply_action(ACT_BACK); queue_redraw(); }
	bool debug_is_editing() const { return editing; }
	String debug_edit_buffer() const { return edit_buffer; }
	// Test hook: breaks the active charge internally, standing in for RMB being
	// released while the engine never delivered the release.
	void debug_break_hold();
	// Largest |pos| across live targets, and the playfield's outer bound.
	float debug_farthest_target() const;
	float debug_spawn_reach() const;
	// True when a live target sits outside the visible playfield.
	bool debug_any_target_unreachable() const;
	// One-line dump of every live target, for diagnosing stuck state.
	PackedStringArray debug_target_dump() const;
	void sync_input_from_tuning();
	bool is_holding_focus() const;
	void debug_set_lifetime(float v) { tuning.base_lifetime = v; tuning.min_lifetime = v; }
	void debug_reset() { reset_session(); }
protected:
	static void _bind_methods();

private:
	Tuning tuning;
	DDNetInput input;
	SessionStats stats;
	Vector<Target> targets;

	Vector2 player_pos;
	Vector2 screen_center;
	float px_per_unit = 1.0f;

	float play_time = 0.0f;
	// Wall-clock origin of the session, so shots logged by the OS can be placed
	// on the same timeline for inter-shot interval statistics.
	float session_origin = 0.0f;
	// Time since the session started. Separate from `play_time` so difficulty can
	// be measured against it directly even if play_time gains other meanings.
	float session_time = 0.0f;
	float current_max_size = 70.0f;
	float spawn_timer = 0.0f;
	float spawn_interval = 0.35f;

	// When set, the next spawn_target() forces this colour (test hook only).
	int timing_target_gray = -1;

	bool rmb_held = false;
	// Index into `targets`; -1 when no hold is active. An index (not a pointer)
	// is used because spawning appends to the vector and can reallocate it.
	int holding_index = -1;
	// True when the current hold was armed by a shot that already counted as a
	// hit, so the release must not bill a second interaction for it.
	bool hold_scored = false;
	// Focus bookkeeping. Mouse state is never polled (see _notification): the
	// engine's polled button state does not see events pushed by scripts, and an
	// unfocused window reports "button up" forever, which used to cancel every
	// charge. This flag is informational.
	bool focus_lost = false;
	float last_interaction_time = 0.0f;
	int last_score = 0;

	bool paused = false;
	Screen screen = Screen::GAME;

	// Test hook: when set, targets stop moving so aim tests are repeatable.
	bool freeze_motion = false;
	// Test hook: when set, the spawn loop stops topping the field up, so a test
	// can observe one target in isolation.
	bool spawn_suppressed = false;

	// Feedback effects
	struct Pop {
		Vector2 pos;
		float age = 0.0f;
		int color = 0;
		bool miss = false;
		// Score value floated up from the hit position (0 = no number).
		int score = 0;
	};
	Vector<Pop> pops;

	// Shatter fragments: spawned when a target is removed (hit or timeout).
	struct Debris {
		Vector2 pos;
		Vector2 vel;
		float size = 0.0f;
		float age = 0.0f;
		float life = 0.0f;
		float spin = 0.0f;
		float angle = 0.0f;
		bool gray = false;
	};
	Vector<Debris> debris;

	// --- Laser presentation state ---------------------------------------------
	//
	// The beam runs from the player point (screen centre) through the cursor and
	// past it. `laser_dir` is the unit direction, `laser_reach` how far it is
	// drawn, `laser_end` the resulting world-space tip.
	Vector2 laser_dir = Vector2(1, 0);
	float laser_reach = 0.0f;
	Vector2 laser_end;
	bool laser_lock = false;
	// Decays every frame after a shot; drives the sweep animation and muzzle
	// bloom. Set to 1.0 when the beam fires.
	float fire_flash = 0.0f;

	// Enemy-ish silhouette glow pulse after taking a hit.
	struct HitFlash {
		Vector2 pos;
		float radius = 0.0f;
		float age = 0.0f;
		float life = 0.3f;
		bool gray = false;
	};
	Vector<HitFlash> hit_flashes;

	// Sparks: travel outward along the beam, subtly drawn toward the impact.
	struct Spark {
		Vector2 pos;
		float age = 0.0f;
		float life = 0.4f;
		float size = 2.0f;
		float speed = 0.0f;
		float offset = 0.0f;
	};
	Vector<Spark> sparks;

	// Expanding rings at impact / miss points.
	struct Ring {
		Vector2 pos;
		float age = 0.0f;
		float life = 0.45f;
		float from_r = 4.0f;
		float to_r = 40.0f;
		float width = 2.5f;
		Color col;
	};
	Vector<Ring> rings;

	// Small "X" ejected from a resolved target.
	struct HitMarker {
		Vector2 pos;
		Vector2 vel;
		float age = 0.0f;
		float life = 0.35f;
		float size = 9.0f;
		bool miss = false;
	};
	Vector<HitMarker> hit_markers;

	// Screen shake: radius in pixels, decays every frame.
	float shake_amount = 0.0f;
	Vector2 shake_offset;
	// Drives reticle spin, beam shimmer and the background sweep.
	float effects_clock = 0.0f;

	// Ambient background dust.
	struct Mote {
		Vector2 pos;
		Vector2 vel;
		float size = 0.0f;
		float phase = 0.0f;
	};
	Vector<Mote> motes;
	void init_motes();

	// Settings menu.
	struct Button {
		Rect2 rect;
		String label;
		String value;
		int action = 0;
	};
	Vector<Button> buttons;
	// Non-selectable section headings, positioned alongside the rows.
	struct SectionLabel {
		Vector2 pos;
		String text;
		// Right edge the heading's rule should run to (its own column).
		float rule_end = 0.0f;
	};
	Vector<SectionLabel> section_labels;
	// Top edge of the menu block, so the heading can be placed above it.
	float menu_top = 0.0f;
	// Index into `buttons` of the selected row.
	int sel_row = 0;
	Screen return_screen = Screen::GAME;
	// When editing is true, keystrokes are consumed as a number for the
	// selected row instead of navigating the menu.
	bool editing = false;
	String edit_buffer;
	// When editing a colour row, `edit_buffer` holds "#rrggbb" text instead of a
	// number, and ENTER parses it as a colour.
	bool edit_is_color = false;
	void build_menu();
	void draw_menu();
	void apply_action(int action);
	void adjust_selected(int direction);
	void set_fps(int fps);
	void handle_edit_key(int keycode);
	void set_resolution(int w, int h);
	void set_window_mode(int mode);
	// Which rows accept typed input (a number, or a hex colour) on ENTER.
	bool row_is_editable(int action) const;
	// True when that input is a colour rather than a number.
	bool row_edits_color(int action) const;
	float last_mouse_pos = -1.0f;

	void load_config();
	void save_config();
	// Pushes tuning values that are cached in live game state (spawn size cap,
	// and targets that would now sit outside the visible field).
	void apply_tuning_changes();

	void update_layout();
	Vector2 to_screen(const Vector2 &ddnet_pos) const;
	Vector2 to_ddnet(const Vector2 &screen_pos) const;
	void draw_hud();
	void draw_target(const Target &t);
	// Base colour a target is painted with: the configured gray or white colour.
	Color base_color(bool gray) const;
	void draw_trajectory(const Target &t);
	void draw_results();
	void draw_laser();
	void draw_effects();

	void spawn_target();
	void update_targets(double delta);
	void resolve_mouse_click(const Vector2 &p, bool left_button);
	void update_hold(double delta);
	void on_hold_started(Target &t);
	// Drops the active charge without scoring: the target stays usable and its
	// lifetime keeps running, so nothing can be pinned on the field.
	void abandon_hold();
	void on_hold_released(bool released_by_timeout);
	void score_target(Target &t, bool success, const Vector2 &cursor);

	void check_input_for_hold(const Vector2 &p);
	Target *find_target_at(const Vector2 &p);
	Target *find_nearest(const Vector2 &p, float &distance);

	// Laser aiming: the beam leaves the screen centre through `dir` and the
	// closest target it crosses is returned, with the beam entry point in
	// `hit_point` and the distance from the aim line in `lateral`.
	Target *find_laser_target(const Vector2 &dir, Vector2 &hit_point, float &lateral);
	void update_laser_geometry();
	void update_effects(double delta);
	void on_target_hit(Target &t);
	float laser_max_reach() const;
	// Distance from the player point to the far corner of the playfield: the
	// longest shot the beam ever has to make (DDNet units).
	float playfield_radius() const;

	void add_pop(const Vector2 &p, bool miss);
	void add_pop(const Vector2 &p, bool miss, int score);
	void add_hit_flash(const Vector2 &p, float radius, bool gray);
	void add_ring(const Vector2 &p, const Color &col, float from_r, float to_r, float life);
	void add_hit_marker(const Vector2 &p, bool miss);
	void add_miss_record(const Vector2 &cursor);
	void spawn_debris(const Vector2 &p, float radius, bool gray, bool outward_only);
	void spawn_sparks(const Vector2 &p, int count);
	void clear_target(Target &t, bool shatter);

	float compute_distance_bonus(const Vector2 &p) const;
	float compute_score(const Target &t) const;
	void update_difficulty(double delta);
	// 0 at the start of a session, 1 once the difficulty ramp is fully applied.
	float difficulty_level() const;
	// Required hold time and release window for a target spawned right now.
	float current_hold_time() const;
	float current_hold_tolerance() const;
};

} // namespace godot
