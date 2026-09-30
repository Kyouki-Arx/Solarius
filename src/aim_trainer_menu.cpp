#include "aim_trainer.h"

#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/math.hpp>
#include <cmath>

using namespace godot;

static DisplayServer *ds() { return DisplayServer::get_singleton(); }

static const int RESOLUTIONS[][2] = {
	{ 1280, 800 },
	{ 1366, 768 },
	{ 1600, 900 },
	{ 1920, 1080 },
	{ 2560, 1440 },
};
static const int RES_COUNT = 5;
static const char *MODE_NAMES[] = { "Windowed", "Borderless", "Fullscreen" };
static const int MODE_COUNT = 3;

static int cur_fps_index() {
	const int f = Engine::get_singleton()->get_max_fps();
	if(f <= 0) {
		return 0; // uncapped
	}
	return f;
}


// Settings persistence. Values live in user://aimtrainer.cfg so they survive a
// relaunch; a missing or unreadable file just leaves the defaults in place.
static const char *CFG_PATH = "user://aimtrainer.cfg";

// Every tunable that the settings menu exposes, plus the display state. Ranges
// are enforced on load so a hand-edited file cannot produce an unusable field.
static float cfg_clamp(float v, float lo, float hi) {
	return CLAMP(v, lo, hi);
}

// Reads `key` from `section`, falling back to the legacy single "settings"
// section so configs written by older builds keep working.
static Variant cfg_get(const Ref<ConfigFile> &cf, const String &section, const String &key, const Variant &def) {
	if(cf->has_section_key(section, key)) {
		return cf->get_value(section, key, def);
	}
	if(cf->has_section_key("settings", key)) {
		return cf->get_value("settings", key, def);
	}
	return def;
}

// A colour as "#rrggbb", for display and for hex entry.
static String color_hex(const Color &c) {
	return "#" + c.to_html(false);
}

// Reads a "#rrggbb" / "rrggbb" string (3, 6 or 8 digits, with or without '#').
// Returns false when the text is not a usable colour, so callers can keep the
// previous value instead of blanking it.
static bool parse_hex_color(const String &text, Color &out) {
	String s = text.strip_edges();
	if(s.begins_with("#")) {
		s = s.substr(1);
	}
	if(s.is_empty() || !s.is_valid_hex_number(false)) {
		return false;
	}
	switch(s.length()) {
		case 3:
		case 6:
		case 8:
			out = Color(s);
			out.a = 1.0f;
			return true;
		default:
			return false;
	}
}

// A colour may be stored either as a Color (what this build writes) or as a
// "#rrggbb" string, so a hand-edited config stays usable.
static Color cfg_color(const Ref<ConfigFile> &cf, const String &section, const String &key, const Color &def) {
	const Variant v = cfg_get(cf, section, key, Variant());
	if(v.get_type() == Variant::NIL) {
		return def;
	}
	if(v.get_type() == Variant::COLOR) {
		Color c = v;
		c.a = 1.0f;
		return c;
	}
	if(v.get_type() == Variant::STRING) {
		Color parsed;
		if(parse_hex_color(v, parsed)) {
			return parsed;
		}
	}
	return def;
}

void AimTrainer::load_config() {
	Ref<ConfigFile> cf;
	cf.instantiate();
	if(cf->load(CFG_PATH) != OK) {
		return;
	}

	// --- input ---
	tuning.mouse_sens = cfg_clamp((float)cfg_get(cf, "input", "mouse_sens", (double)tuning.mouse_sens), 1.0f, 100000.0f);
	tuning.mouse_max_distance = cfg_clamp((float)cfg_get(cf, "input", "mouse_max_distance", (double)tuning.mouse_max_distance), 50.0f, 2000.0f);

	// --- gameplay ---
	tuning.target_count = (int)CLAMP((double)cfg_get(cf, "gameplay", "target_count", (double)tuning.target_count), 1.0, 12.0);
	tuning.max_size = cfg_clamp((float)cfg_get(cf, "gameplay", "max_target_size", (double)tuning.max_size), tuning.min_size, 200.0f);
	tuning.spawn_interval = cfg_clamp((float)cfg_get(cf, "gameplay", "spawn_interval", (double)tuning.spawn_interval), 0.1f, 10.0f);
	// `laser_free_extend` from older builds is read but no longer used: the beam
	// is now sized from the playfield, which targets fill.
	(void)cfg_get(cf, "gameplay", "laser_free_extend", 0.0);

	// --- hold ---
	// hold_initial may have been stored as `hold_max` by an older build.
	const double hold_def = (double)MAX(tuning.hold_initial, tuning.hold_min);
	tuning.hold_initial = cfg_clamp((float)cfg_get(cf, "hold", "hold_initial",
									   cfg_get(cf, "hold", "hold_max", hold_def)),
			0.10f, 10.0f);
	tuning.hold_min = cfg_clamp((float)cfg_get(cf, "hold", "hold_min", (double)tuning.hold_min), 0.10f, 10.0f);
	// The floor can never exceed the starting value, or the ramp would run
	// backwards.
	tuning.hold_min = MIN(tuning.hold_min, tuning.hold_initial);
	tuning.hold_tolerance_frac = cfg_clamp((float)cfg_get(cf, "hold", "hold_tolerance_frac", (double)tuning.hold_tolerance_frac), 0.0f, 1.0f);
	tuning.hold_tolerance_min = cfg_clamp((float)cfg_get(cf, "hold", "hold_tolerance_min", (double)tuning.hold_tolerance_min), 0.0f, 2.0f);
	tuning.hold_tolerance_min = MIN(tuning.hold_tolerance_min, MAX(tuning.hold_initial, tuning.hold_min));

	// --- targets ---
	tuning.gray_chance = cfg_clamp((float)cfg_get(cf, "targets", "gray_chance", (double)tuning.gray_chance), 0.0f, 1.0f);
	tuning.color_gray = cfg_color(cf, "targets", "color_gray", tuning.color_gray);
	tuning.color_white = cfg_color(cf, "targets", "color_white", tuning.color_white);

	// --- display ---
	const int fps = (int)cfg_get(cf, "display", "max_fps", 0.0);
	set_fps(fps);

	const int mode = (int)cfg_get(cf, "display", "window_mode", -1);
	if(mode >= 0) {
		set_window_mode(mode);
	}
	// Resolution is applied last: changing it can recreate/resize the window,
	// so the size has to be set after the mode is already correct.
	const int w = (int)cfg_get(cf, "display", "window_width", 0);
	const int h = (int)cfg_get(cf, "display", "window_height", 0);
	if(w > 0 && h > 0) {
		// Only meaningful while windowed; a fullscreen mode owns the size.
		const int m = ds()->window_get_mode();
		if(m == DisplayServer::WINDOW_MODE_WINDOWED || m == DisplayServer::WINDOW_MODE_MINIMIZED) {
			set_resolution(w, h);
		}
	}

	apply_tuning_changes();
	sync_input_from_tuning();
	update_layout();
	build_menu();
}

void AimTrainer::save_config() {
	Ref<ConfigFile> cf;
	cf.instantiate();

	// Display.
	cf->set_value("display", "max_fps", (double)Engine::get_singleton()->get_max_fps());
	// Mode is stored as our 0/1/2 index, not the raw DisplayServer enum, so the
	// borderless combination survives a round trip.
	int mode_index = 0;
	const int m = ds()->window_get_mode();
	const bool borderless = ds()->window_get_flag(DisplayServer::WINDOW_FLAG_BORDERLESS);
	if(borderless && m == DisplayServer::WINDOW_MODE_FULLSCREEN) {
		mode_index = 1;
	} else if(m == DisplayServer::WINDOW_MODE_EXCLUSIVE_FULLSCREEN) {
		mode_index = 2;
	}
	cf->set_value("display", "window_mode", mode_index);
	const Vector2i size = ds()->window_get_size();
	cf->set_value("display", "window_width", size.x);
	cf->set_value("display", "window_height", size.y);

	// Input.
	cf->set_value("input", "mouse_sens", (double)tuning.mouse_sens);
	cf->set_value("input", "mouse_max_distance", (double)tuning.mouse_max_distance);

	// Gameplay.
	cf->set_value("gameplay", "target_count", tuning.target_count);
	cf->set_value("gameplay", "max_target_size", (double)tuning.max_size);
	cf->set_value("gameplay", "spawn_interval", (double)tuning.spawn_interval);

	// Hold timing.
	cf->set_value("hold", "hold_initial", (double)tuning.hold_initial);
	cf->set_value("hold", "hold_min", (double)tuning.hold_min);
	cf->set_value("hold", "hold_tolerance_frac", (double)tuning.hold_tolerance_frac);
	cf->set_value("hold", "hold_tolerance_min", (double)tuning.hold_tolerance_min);

	// Target appearance and mix.
	cf->set_value("targets", "gray_chance", (double)tuning.gray_chance);
	cf->set_value("targets", "color_gray", tuning.color_gray);
	cf->set_value("targets", "color_white", tuning.color_white);

	// `save` returns an Error; a failed write cannot be acted on here, and the
	// settings stay live in memory either way.
	(void)cf->save(CFG_PATH);
}

// Applies the parts of `tuning` that have to be pushed into live game state
// (rather than read on demand), so a settings change takes effect immediately
// without needing a session reset.
void AimTrainer::apply_tuning_changes() {
	current_max_size = CLAMP(current_max_size, tuning.min_size, tuning.max_size);
	// Spawn pacing is recomputed from `tuning` every frame, so a change takes
	// effect on the next spawn; nothing to push here.
	// A target that now sits outside the visible field is pulled back inside it.
	const float hx = tuning.field_width * 0.5f;
	const float hy = tuning.field_height * 0.5f;
	for(int i = 0; i < targets.size(); i++) {
		Target &t = targets.ptrw()[i];
		if(!t.active) {
			continue;
		}
		const Vector2 before = t.pos;
		t.pos.x = CLAMP(t.pos.x, -hx, hx);
		t.pos.y = CLAMP(t.pos.y, -hy, hy);
		if((t.pos - before).length_squared() > 0.0001f) {
			t.start_pos = t.pos;
		}
	}
	// Shrinking the field below the live target count is allowed: the extras
	// simply expire and are not replaced.
}

void AimTrainer::set_fps(int fps) {
	Engine::get_singleton()->set_max_fps(fps <= 0 ? 0 : fps);
	save_config();
}

void AimTrainer::set_resolution(int w, int h) {
	const Vector2i s(w, h);
	ds()->window_set_size(s);
	// Re-centre the window if it is currently in a windowed mode.
	if(ds()->window_get_mode() == DisplayServer::WINDOW_MODE_WINDOWED ||
			ds()->window_get_mode() == DisplayServer::WINDOW_MODE_MINIMIZED) {
		const Rect2i screen = Rect2i(ds()->screen_get_position(), ds()->screen_get_size());
		ds()->window_set_position(screen.position + (screen.size - s) / 2);
	}
	update_layout();
	save_config();
}

void AimTrainer::set_window_mode(int mode) {
	switch(mode) {
		case 1:
			// Borderless windowed: fullscreen-sized, still a desktop window.
			ds()->window_set_flag(DisplayServer::WINDOW_FLAG_BORDERLESS, true);
			ds()->window_set_mode(DisplayServer::WINDOW_MODE_FULLSCREEN);
			break;
		case 2:
			ds()->window_set_flag(DisplayServer::WINDOW_FLAG_BORDERLESS, false);
			ds()->window_set_mode(DisplayServer::WINDOW_MODE_EXCLUSIVE_FULLSCREEN);
			break;
		default:
			ds()->window_set_flag(DisplayServer::WINDOW_FLAG_BORDERLESS, false);
			ds()->window_set_mode(DisplayServer::WINDOW_MODE_WINDOWED);
			break;
	}
	update_layout();
	save_config();
}

// Rows that accept typed input (ENTER opens the editor): numbers, plus the two
// colour rows which take a hex string. Everything else is stepped with A/D or
// the arrow keys only.
bool AimTrainer::row_is_editable(int action) const {
	switch(action) {
		case ACT_FPS:
		case ACT_SENS:
		case ACT_MAXDIST:
		case ACT_TARGETS:
		case ACT_MAXSIZE:
		case ACT_SPAWN:
		case ACT_HOLD:
		case ACT_HOLD_MIN:
		case ACT_TOLERANCE:
		case ACT_GRAY_CHANCE:
		case ACT_COLOR_GRAY:
		case ACT_COLOR_WHITE:
			return true;
		default:
			return false;
	}
}

// True when the row is edited as a colour rather than as a number.
bool AimTrainer::row_edits_color(int action) const {
	return action == ACT_COLOR_GRAY || action == ACT_COLOR_WHITE;
}

void AimTrainer::build_menu() {
	buttons.clear();
	section_labels.clear();
	const Vector2 size = get_viewport_rect().size;
	const float h = 40.0f;
	const float gap = 10.0f;
	// The settings grew past one comfortable column, so they are laid out in two.
	const float col_gap = 26.0f;
	const float col_w = MIN(460.0f, MAX(300.0f, (size.x - 120.0f - col_gap) * 0.5f));
	const float total_w = col_w * 2.0f + col_gap;
	const float col_x[2] = { (size.x - total_w) * 0.5f, (size.x - total_w) * 0.5f + col_w + col_gap };

	// Two passes: build both columns, then place them now that the height is
	// known. `col` picks the column and `lead_gap` the blank line that separates
	// a section from the one above it.
	enum Col { LEFT, RIGHT };
	struct Row {
		String label;
		String value;
		int action;
		int section; // -1 = none
		Col col;
		float lead_gap;
	};
	Vector<Row> rows;
	Vector<String> sections;

	auto add_row = [&](const String &label, const String &value, int action, const String &section,
						   Col col, float lead_gap) {
		Row r;
		r.label = label;
		r.value = value;
		r.action = action;
		r.col = col;
		r.lead_gap = lead_gap;
		if(section != "") {
			sections.push_back(section);
			r.section = sections.size() - 1;
		} else {
			r.section = -1;
		}
		rows.push_back(r);
	};

	const int fps = cur_fps_index();
	add_row("FPS limit", fps <= 0 ? String("Unlimited") : String::num(fps), ACT_FPS, "DISPLAY", RIGHT, 0.0f);

	int res_index = 0;
	const Vector2i cur = ds()->window_get_size();
	for(int i = 0; i < RES_COUNT; i++) {
		if(RESOLUTIONS[i][0] == cur.x && RESOLUTIONS[i][1] == cur.y) {
			res_index = i;
		}
	}
	add_row("Resolution", String::num(RESOLUTIONS[res_index][0]) + "x" + String::num(RESOLUTIONS[res_index][1]), ACT_RES, "", RIGHT, 0.0f);

	int mode_index = 0;
	const int m = ds()->window_get_mode();
	const bool borderless = ds()->window_get_flag(DisplayServer::WINDOW_FLAG_BORDERLESS);
	if(borderless && m == DisplayServer::WINDOW_MODE_FULLSCREEN) {
		mode_index = 1;
	} else if(m == DisplayServer::WINDOW_MODE_EXCLUSIVE_FULLSCREEN) {
		mode_index = 2;
	}
	add_row("Window mode", MODE_NAMES[mode_index], ACT_MODE, "", RIGHT, 0.0f);

	add_row("Targets on field", String::num(tuning.target_count), ACT_TARGETS, "GAMEPLAY", RIGHT, 16.0f);
	add_row("Max target size", String::num(tuning.max_size, 0) + " px", ACT_MAXSIZE, "", RIGHT, 0.0f);
	add_row("Spawn gap", String::num(tuning.spawn_interval, 2) + " s", ACT_SPAWN, "", RIGHT, 0.0f);
	add_row("Hold at start", String::num(tuning.hold_initial, 2) + " s", ACT_HOLD, "", RIGHT, 0.0f);
	add_row("Hold minimum", String::num(tuning.hold_min, 2) + " s", ACT_HOLD_MIN, "", RIGHT, 0.0f);
	add_row("Tolerance at start", String::num(current_hold_tolerance(), 2) + " s", ACT_TOLERANCE, "", RIGHT, 0.0f);

	add_row("Gray target", color_hex(tuning.color_gray), ACT_COLOR_GRAY, "APPEARANCE", LEFT, 0.0f);
	add_row("White target", color_hex(tuning.color_white), ACT_COLOR_WHITE, "", LEFT, 0.0f);
	add_row("Gray spawn chance", String::num(tuning.gray_chance * 100.0f, 0) + " %", ACT_GRAY_CHANCE, "", LEFT, 0.0f);

	add_row("Sensitivity (inp_mousesens)", String::num(tuning.mouse_sens, 0), ACT_SENS, "INPUT", LEFT, 16.0f);
	add_row("Cursor max distance", String::num(tuning.mouse_max_distance, 0), ACT_MAXDIST, "", LEFT, 0.0f);

	add_row("Back", "", ACT_BACK, "", LEFT, 16.0f);

	// Vertical layout: heading height above each section, gap between rows. Each
	// column is measured and placed independently.
	const float head_h = 26.0f;
	float col_h[2] = { 0.0f, 0.0f };
	for(int i = 0; i < rows.size(); i++) {
		if(rows[i].section >= 0) {
			col_h[rows[i].col] += rows[i].lead_gap + head_h;
		}
		col_h[rows[i].col] += h + gap;
	}
	col_h[0] -= gap;
	col_h[1] -= gap;
	const float block = MAX(col_h[0], col_h[1]);
	// Both columns start at the same height; the shorter one is not pushed down,
	// because that put the heading far above its own first row.
	const float top = MAX(72.0f, (size.y - block) * 0.5f);
	float y[2] = { top, top };
	menu_top = top;

	for(int i = 0; i < rows.size(); i++) {
		const Col cidx = rows[i].col;
		if(rows[i].section >= 0) {
			y[cidx] += rows[i].lead_gap;
			SectionLabel sl;
			sl.pos = Vector2(col_x[cidx] + 4.0f, y[cidx] + head_h * 0.5f + 5.0f);
			sl.text = sections[rows[i].section];
			sl.rule_end = col_x[cidx] + col_w;
			section_labels.push_back(sl);
			y[cidx] += head_h;
		}
		Button b;
		b.rect = Rect2(col_x[cidx], y[cidx], col_w, h);
		b.label = rows[i].label;
		b.value = rows[i].value;
		b.action = rows[i].action;
		buttons.push_back(b);
		y[cidx] += h + gap;
	}
}


void AimTrainer::adjust_selected(int direction) {
	if(sel_row < 0 || sel_row >= buttons.size()) {
		return;
	}
	const int action = buttons[sel_row].action;
	switch(action) {
		case ACT_FPS: {
			// 0 = uncapped, then 60, 120, 144, 165, 240, 500, 1000, 2000.
			static const int values[] = { 0, 60, 120, 144, 165, 240, 500, 1000, 2000 };
			int idx = 0;
			for(int i = 0; i < 9; i++) {
				if(values[i] == cur_fps_index()) {
					idx = i;
				}
			}
			idx = CLAMP(idx + direction, 0, 8);
			set_fps(values[idx]);
		} break;
		case ACT_RES: {
			int res_index = 0;
			const Vector2i cur = ds()->window_get_size();
			for(int i = 0; i < RES_COUNT; i++) {
				if(RESOLUTIONS[i][0] == cur.x && RESOLUTIONS[i][1] == cur.y) {
					res_index = i;
				}
			}
			res_index = CLAMP(res_index + direction, 0, RES_COUNT - 1);
			set_resolution(RESOLUTIONS[res_index][0], RESOLUTIONS[res_index][1]);
		} break;
		case ACT_MODE: {
			int mode_index = 0;
			const int m = ds()->window_get_mode();
			const bool borderless = ds()->window_get_flag(DisplayServer::WINDOW_FLAG_BORDERLESS);
			if(borderless && m == DisplayServer::WINDOW_MODE_FULLSCREEN) {
				mode_index = 1;
			} else if(m == DisplayServer::WINDOW_MODE_EXCLUSIVE_FULLSCREEN) {
				mode_index = 2;
			}
			mode_index = CLAMP(mode_index + direction, 0, MODE_COUNT - 1);
			set_window_mode(mode_index);
		} break;
		case ACT_SENS: {
			// DDNet style: 1..100000, default 200.
			const float step = tuning.mouse_sens < 200.0f ? 5.0f : 10.0f;
			tuning.mouse_sens = CLAMP(tuning.mouse_sens + step * (float)direction, 1.0f, 100000.0f);
		} break;
		case ACT_MAXDIST: {
			// cl_mouse_max_distance range is 0..5000 in DDNet.
			const float step = tuning.mouse_max_distance < 400.0f ? 10.0f : 25.0f;
			tuning.mouse_max_distance = CLAMP(tuning.mouse_max_distance + step * (float)direction, 50.0f, 2000.0f);
			sync_input_from_tuning();
		} break;
		case ACT_TARGETS: {
			tuning.target_count = CLAMP(tuning.target_count + direction, 1, 12);
		} break;
		case ACT_MAXSIZE: {
			tuning.max_size = CLAMP(tuning.max_size + 2.0f * (float)direction, tuning.min_size, 200.0f);
			apply_tuning_changes();
		} break;
		case ACT_SPAWN: {
			// Coarser steps are fine; the value is shown to two decimals.
			tuning.spawn_interval = CLAMP(tuning.spawn_interval + 0.05f * (float)direction, 0.20f, 5.0f);
			tuning.spawn_interval_min = MIN(tuning.spawn_interval_min, tuning.spawn_interval);
		} break;
		case ACT_HOLD: {
			// Start-of-run charge; it can never fall below the floor it ramps to.
			tuning.hold_initial = CLAMP(tuning.hold_initial + 0.1f * (float)direction, tuning.hold_min, 10.0f);
		} break;
		case ACT_HOLD_MIN: {
			// The floor; it can never rise above the starting value.
			tuning.hold_min = CLAMP(tuning.hold_min + 0.1f * (float)direction, 0.10f, tuning.hold_initial);
		} break;
		case ACT_TOLERANCE: {
			// Steps a hundredth of the start-of-run hold, so it stays readable.
			const float step = MAX(tuning.hold_initial, tuning.hold_min) * 0.01f;
			tuning.hold_tolerance_frac = CLAMP(tuning.hold_tolerance_frac + step * (float)direction, 0.0f, 1.0f);
		} break;
		case ACT_COLOR_GRAY:
		case ACT_COLOR_WHITE: {
			// A/D slides the brightness of the current colour, keeping its hue, so
			// a shade can be nudged. ENTER types an exact hex for anything else.
			const Color cur = action == ACT_COLOR_GRAY ? tuning.color_gray : tuning.color_white;
			const float step = 0.04f * (float)direction;
			Color next = Color(CLAMP(cur.r + step, 0.0f, 1.0f), CLAMP(cur.g + step, 0.0f, 1.0f),
					CLAMP(cur.b + step, 0.0f, 1.0f));
			next.a = 1.0f;
			if(action == ACT_COLOR_GRAY) {
				tuning.color_gray = next;
			} else {
				tuning.color_white = next;
			}
		} break;
		case ACT_GRAY_CHANCE: {
			tuning.gray_chance = CLAMP(tuning.gray_chance + 0.05f * (float)direction, 0.0f, 1.0f);
		} break;
		default:
			break;
	}
	sync_input_from_tuning();
	save_config();
	build_menu();
}

void AimTrainer::apply_action(int action) {
	if(action == ACT_BACK) {
		screen = return_screen;
		if(screen == Screen::GAME) {
			paused = false;
			inp()->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
		}
		build_menu();
	}
}

void AimTrainer::init_motes() {
	motes.clear();
	const Vector2 size = get_viewport_rect().size;
	for(int i = 0; i < 70; i++) {
		Mote m;
		m.pos = Vector2((float)rand() / (float)RAND_MAX * size.x, (float)rand() / (float)RAND_MAX * size.y);
		m.vel = Vector2(((float)rand() / (float)RAND_MAX - 0.5f) * 26.0f, ((float)rand() / (float)RAND_MAX - 0.5f) * 26.0f);
		m.size = 1.0f + (float)rand() / (float)RAND_MAX * 2.5f;
		m.phase = (float)rand() / (float)RAND_MAX * Math_TAU;
		motes.push_back(m);
	}
}

void AimTrainer::spawn_debris(const Vector2 &p, float radius, bool gray, bool outward_only) {
	const int count = 10 + (int)(radius / 6.0f);
	for(int i = 0; i < count; i++) {
		Debris d;
		d.pos = p;
		const float a = (float)rand() / (float)RAND_MAX * Math_TAU;
		const float speed = (60.0f + (float)rand() / (float)RAND_MAX * 260.0f) * (outward_only ? 1.0f : 0.55f);
		d.vel = Vector2(cos(a), sin(a)) * speed;
		d.size = 2.0f + (float)rand() / (float)RAND_MAX * 4.5f;
		d.age = 0.0f;
		d.life = 0.5f + (float)rand() / (float)RAND_MAX * 0.7f;
		d.spin = ((float)rand() / (float)RAND_MAX - 0.5f) * 12.0f;
		d.angle = (float)rand() / (float)RAND_MAX * Math_TAU;
		d.gray = gray;
		debris.push_back(d);
	}
}

void AimTrainer::clear_target(Target &t, bool shatter) {
	if(shatter) {
		spawn_debris(to_screen(t.pos), t.radius * px_per_unit, t.gray, true);
	}
	t.active = false;
	t.holding = false;
}

// Typed entry for settings. Numbers accept digits 0-9; colour rows accept a hex
// string. ENTER applies, ESC cancels.
void AimTrainer::handle_edit_key(int keycode) {
	if(keycode == KEY_ESCAPE) {
		editing = false;
		edit_buffer = "";
		edit_is_color = false;
		return;
	}
	if(keycode == KEY_ENTER || keycode == KEY_KP_ENTER) {
		const int act = sel_row >= 0 && sel_row < buttons.size() ? buttons[sel_row].action : 0;
		if(edit_is_color) {
			Color parsed;
			if(parse_hex_color(edit_buffer, parsed)) {
				if(act == ACT_COLOR_GRAY) {
					tuning.color_gray = parsed;
				} else if(act == ACT_COLOR_WHITE) {
					tuning.color_white = parsed;
				}
			}
			// Unparseable text is simply discarded; the colour stays as it was.
		} else if(edit_buffer.is_valid_int()) {
			const int v = edit_buffer.to_int();
			switch(act) {
				case ACT_FPS:
					set_fps(v);
					break;
				case ACT_SENS:
					// inp_mousesens is 1..100000 in DDNet.
					tuning.mouse_sens = (float)CLAMP(v, 1, 100000);
					sync_input_from_tuning();
					break;
				case ACT_MAXDIST:
					// cl_mouse_max_distance; kept below DDNet's 5000 ceiling
					// because the playfield and target radii are tuned for it.
					tuning.mouse_max_distance = (float)CLAMP(v, 50, 2000);
					sync_input_from_tuning();
					break;
				case ACT_TARGETS:
					tuning.target_count = CLAMP(v, 1, 12);
					break;
				case ACT_MAXSIZE:
					tuning.max_size = CLAMP((float)v, tuning.min_size, 200.0f);
					apply_tuning_changes();
					break;
				case ACT_SPAWN:
					// Typed in hundredths of a second, so "110" means 1.10 s.
					tuning.spawn_interval = CLAMP((float)v / 100.0f, 0.20f, 5.0f);
					tuning.spawn_interval_min = MIN(tuning.spawn_interval_min, tuning.spawn_interval);
					break;
				case ACT_HOLD:
					// Hundredths of a second: "250" means 2.50 s.
					tuning.hold_initial = CLAMP((float)v / 100.0f, tuning.hold_min, 10.0f);
					break;
				case ACT_HOLD_MIN:
					tuning.hold_min = CLAMP((float)v / 100.0f, 0.10f, tuning.hold_initial);
					break;
				case ACT_TOLERANCE:
					// Percent of the start-of-run hold, so "30" means 0.30 of it.
					tuning.hold_tolerance_frac = CLAMP((float)v / 100.0f, 0.0f, 1.0f);
					break;
				case ACT_GRAY_CHANCE:
					tuning.gray_chance = CLAMP((float)v / 100.0f, 0.0f, 1.0f);
					break;
				default:
					break;
			}
		}
		editing = false;
		edit_buffer = "";
		edit_is_color = false;
		save_config();
		build_menu();
		return;
	}
	if(keycode == KEY_BACKSPACE) {
		if(!edit_buffer.is_empty()) {
			edit_buffer = edit_buffer.substr(0, edit_buffer.length() - 1);
		}
		return;
	}
	if(edit_is_color) {
		// Hex digits, plus '#' so a pasted value is accepted verbatim.
		const bool is_digit = keycode >= KEY_0 && keycode <= KEY_9;
		const bool is_hex_alpha = keycode >= KEY_A && keycode <= KEY_F;
		if((is_digit || is_hex_alpha || keycode == KEY_NUMBERSIGN) && edit_buffer.length() < 9) {
			if(keycode == KEY_NUMBERSIGN) {
				edit_buffer = "#";
			} else {
				edit_buffer += String::chr((char)keycode).to_upper();
			}
		}
		return;
	}
	// Digits 0-9 only for numeric rows.
	if(keycode >= KEY_0 && keycode <= KEY_9) {
		if(edit_buffer.length() < 6) {
			edit_buffer += String::chr('0' + (keycode - KEY_0));
		}
		return;
	}
}
