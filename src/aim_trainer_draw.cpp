#include "aim_trainer.h"

#include <godot_cpp/classes/canvas_item.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <cmath>

using namespace godot;

// Colour helpers for the configurable target colours. Mixing towards white or
// black keeps every derived shade (rim, glow, flash, debris) consistent with
// whatever base colour the player picked.
static Color toward_white(const Color &c, float t) {
	return c.lerp(Color(1.0f, 1.0f, 1.0f, c.a), t);
}

static Color toward_black(const Color &c, float t) {
	return c.lerp(Color(0.0f, 0.0f, 0.0f, c.a), t);
}

// A Glow tint derived from the base colour, lifted so a dark base still reads
// as a glow rather than a smudge.
static Color glow_tint(const Color &c) {
	return toward_white(c, 0.55f);
}

// Soft radial bloom built from concentric discs. Layering translucent discs is
// cheaper than a shader and works in the CanvasItem draw path directly.
static void draw_bloom(CanvasItem *ci, const Vector2 &c, float radius, const Color &col, int steps = 12) {
	if(radius <= 0.5f) {
		return;
	}
	for(int i = steps; i >= 1; i--) {
		const float k = (float)i / (float)steps;
		Color c2 = col;
		c2.a = col.a * (1.0f - k) * (1.0f - k);
		ci->draw_circle(c, radius * k, c2);
	}
	// Solid, brighter core.
	Color core = col;
	core.a = MIN(1.0f, col.a * 2.6f);
	ci->draw_circle(c, MAX(1.0f, radius * 0.22f), core);
}

Color AimTrainer::base_color(bool gray) const {
	return gray ? tuning.color_gray : tuning.color_white;
}

void AimTrainer::draw_target(const Target &t) {	const Vector2 c = to_screen(t.pos);
	float r = t.radius * px_per_unit;

	// Spawn reveal: the silhouette grows into place instead of popping in, which
	// also means it only becomes hittable-looking once it is actually there.
	const float reveal = CLAMP(t.age / 0.22f, 0.0f, 1.0f);
	if(reveal < 1.0f) {
		const float ease = 1.0f - std::pow(1.0f - reveal, 3.0f);
		r *= 0.45f + 0.55f * ease;
		if(r < 1.0f) {
			return;
		}
		// Ghost outline at the final size, so the arriving shape announces
		// exactly how big it is going to be.
		Color ghost = glow_tint(base_color(t.gray));
		ghost.a = 0.30f * (1.0f - reveal);
		draw_arc(c, t.radius * px_per_unit, 0, Math_TAU, 40, ghost, 1.5f, true);
	}

	const Color base = base_color(t.gray);

	// Impact flash: the silhouette blows out to white for a beat after a hit.
	float flash = 0.0f;
	for(int i = 0; i < hit_flashes.size(); i++) {
		const HitFlash &f = hit_flashes[i];
		if((f.pos - c).length_squared() > (r + 24.0f) * (r + 24.0f)) {
			continue;
		}
		flash = MAX(flash, 1.0f - f.age / MAX(f.life, 0.0001f));
	}

	// Is the beam locked on this one? Locked targets get a hot rim and glow.
	const bool locked = laser_lock && (laser_end - c).length() < r + 10.0f;

	// Outer bloom, derived from the target's own colour.
	const Color tint = glow_tint(base);
	const float pulse = 0.5f + 0.5f * sin(effects_clock * 2.4f + t.pos.x * 0.01f);
	if(locked) {
		draw_bloom(this, c, r * 2.1f, Color(1.0f, 0.35f, 0.30f, 0.30f + 0.18f * pulse));
	} else {
		draw_bloom(this, c, r * 1.55f, Color(tint.r, tint.g, tint.b, 0.10f + 0.06f * pulse));
	}

	// Tinted underlay so the silhouette reads against the dark field, plus a
	// dark backing so debris and motes never show through it.
	if(t.shape == TargetShape::CIRCLE) {
		draw_circle(c, r, Color(0.02f, 0.03f, 0.05f, 0.92f));
		draw_circle(c, r * 0.96f, base);
	} else {
		draw_rect(Rect2(c - Vector2(r, r), Vector2(r * 2.0f, r * 2.0f)), Color(0.02f, 0.03f, 0.05f, 0.92f));
		const float ir = r * 0.94f;
		draw_rect(Rect2(c - Vector2(ir, ir), Vector2(ir * 2.0f, ir * 2.0f)), base);
	}

	// Inner shading: a brighter top-left lip gives the shapes some volume.
	const Color lip = toward_white(base, 0.35f);
	const Color shade = toward_black(base, 0.30f);
	if(t.shape == TargetShape::CIRCLE) {
		draw_circle(c - Vector2(r * 0.28f, r * 0.30f), r * 0.58f, Color(lip.r, lip.g, lip.b, 0.45f));
		draw_arc(c, r * 0.80f, Math_PI * 0.15f, Math_PI * 1.05f, 24, Color(shade.r, shade.g, shade.b, 0.45f), 3.0f, true);
	} else {
		const float b = r * 0.62f;
		draw_rect(Rect2(c - Vector2(r * 0.9f, r * 0.9f), Vector2(b, b)), Color(lip.r, lip.g, lip.b, 0.22f));
		draw_rect(Rect2(c + Vector2(r * 0.3f, r * 0.3f), Vector2(r * 0.60f, r * 0.60f)), Color(shade.r, shade.g, shade.b, 0.22f));
	}

	// Rim.
	const Color rim_col = locked ? Color(1.0f, 0.55f, 0.45f, 0.85f) : Color(1, 1, 1, 0.16f);
	if(t.shape == TargetShape::CIRCLE) {
		draw_arc(c, r, 0, Math_TAU, 48, rim_col, 2.0f, true);
	} else {
		draw_rect(Rect2(c - Vector2(r, r), Vector2(r * 2.0f, r * 2.0f)), rim_col, false, 2.0f);
	}

	// Flash overlay.
	if(flash > 0.01f) {
		const Color f = Color(1, 1, 1, 0.85f * flash);
		if(t.shape == TargetShape::CIRCLE) {
			draw_circle(c, r, f);
		} else {
			draw_rect(Rect2(c - Vector2(r, r), Vector2(r * 2.0f, r * 2.0f)), f);
		}
	}

	// Remaining-life indicator. It previously drew a full circle around every
	// object, which made the squares read as "squares with a circle on them".
	// Now it hugs the actual silhouette: a ring for circles, a corner-bracket
	// frame for squares, and it only appears once time actually starts running
	// down.
	const float life_left = CLAMP(1.0f - t.age / MAX(t.life, 0.0001f), 0.0f, 1.0f);
	const float urgency = 1.0f - life_left;
	if(urgency > 0.02f) {
		// Fades from cool blue toward a hot amber as the timer runs out.
		const Color cool = Color(0.30f, 0.62f, 0.95f);
		const Color hot = Color(1.0f, 0.55f, 0.20f);
		const Color life_col = cool.lerp(hot, urgency * urgency);
		const float blink = urgency > 0.7f ? (0.55f + 0.45f * sin(effects_clock * 14.0f)) : 1.0f;
		Color lc = life_col;
		lc.a = (0.35f + 0.55f * urgency) * blink;
		if(t.shape == TargetShape::CIRCLE) {
			draw_arc(c, r + 4.0f, -Math_PI * 0.5f, -Math_PI * 0.5f + Math_TAU * life_left, 40, lc, 2.5f, true);
		} else {
			// Corner brackets that stay proportional to the square's own size.
			const float arm = MAX(3.0f, r * 0.45f * (0.35f + 0.65f * life_left));
			draw_line(c + Vector2(-r, -r), c + Vector2(-r + arm, -r), lc, 2.5f, true);
			draw_line(c + Vector2(-r, -r), c + Vector2(-r, -r + arm), lc, 2.5f, true);
			draw_line(c + Vector2(r, -r), c + Vector2(r - arm, -r), lc, 2.5f, true);
			draw_line(c + Vector2(r, -r), c + Vector2(r, -r + arm), lc, 2.5f, true);
			draw_line(c + Vector2(-r, r), c + Vector2(-r + arm, r), lc, 2.5f, true);
			draw_line(c + Vector2(-r, r), c + Vector2(-r, r - arm), lc, 2.5f, true);
			draw_line(c + Vector2(r, r), c + Vector2(r - arm, r), lc, 2.5f, true);
			draw_line(c + Vector2(r, r), c + Vector2(r, r - arm), lc, 2.5f, true);
		}
	}

	// Hold progress: fills while RMB is held, completes on release. The
	// indicator traces the target's own outline, so a square never wears a
	// circle around it.
	if(t.gray && t.hold_progress > 0.0f) {
		const float p = CLAMP(t.hold_progress, 0.0f, 1.0f);
		const Color ring = p >= 1.0f ? Color(0.35f, 0.95f, 0.45f) : Color(0.95f, 0.75f, 0.25f);
		const float rr = r + 10.0f;
		if(p >= 1.0f) {
			// Charge complete: a halo that breathes so release timing is readable.
			const float breathe = 0.55f + 0.45f * sin(effects_clock * 9.0f);
			draw_bloom(this, c, r + 26.0f, Color(0.35f, 0.95f, 0.45f, 0.16f * breathe), 8);
		}
		if(t.shape == TargetShape::CIRCLE) {
			draw_arc(c, rr, -Math_PI * 0.5f, -Math_PI * 0.5f + Math_TAU * p, 64, ring, 4.0f, true);
		} else {
			// Fill the square's own outline, one edge at a time, so a square
			// never wears a circle around it. Each edge consumes 1/4 of p.
			const float hw = rr;
			const float x0 = c.x - hw;
			const float y0 = c.y - hw;
			const float side = 2.0f * hw;
			const float e = p * 4.0f;
			const float f1 = CLAMP(e, 0.0f, 1.0f);
			const float f2 = CLAMP(e - 1.0f, 0.0f, 1.0f);
			const float f3 = CLAMP(e - 2.0f, 0.0f, 1.0f);
			const float f4 = CLAMP(e - 3.0f, 0.0f, 1.0f);
			const float x1 = x0 + side;
			const float y1 = y0 + side;
			if(f1 > 0.0f) {
				draw_line(Vector2(x0, y0), Vector2(x0 + side * f1, y0), ring, 4.0f, true);
			}
			if(f2 > 0.0f) {
				draw_line(Vector2(x1, y0), Vector2(x1, y0 + side * f2), ring, 4.0f, true);
			}
			if(f3 > 0.0f) {
				draw_line(Vector2(x1, y1), Vector2(x1 - side * f3, y1), ring, 4.0f, true);
			}
			if(f4 > 0.0f) {
				draw_line(Vector2(x0, y1), Vector2(x0, y1 - side * f4), ring, 4.0f, true);
			}
		}
	}

	// Required hold time as a small marker under gray targets.
	if(t.gray) {
		draw_string(ThemeDB::get_singleton()->get_default_theme()->get_font("font", "Label"),
				c + Vector2(-r, r + 22.0f), String::num(t.hold_time, 2) + "s",
				HORIZONTAL_ALIGNMENT_CENTER, -1, 13, Color(0.75f, 0.75f, 0.78f));
	}
}

// Path preview for a moving target: samples the flight ahead over a bounded
// window and fades it out with distance. Sampling the whole remaining flight
// drew a single hard straight line hundreds of units past the cursor circle,
// which read as a stray laser rather than as a prediction.
void AimTrainer::draw_trajectory(const Target &t) {
	if(!t.moving) {
		return;
	}
	const float reach = input.get_effective_max_distance();
	const float dur = MAX(t.move_duration, 0.0001f);
	// Seconds of flight to preview, capped so the sample step stays small.
	const float window_s = MIN(1.05f, dur);
	const float from_s = CLAMP(t.move_time, 0.0f, dur);
	const float to_s = MIN(from_s + window_s, dur);
	const float step_s = MAX(window_s / 40.0f, 0.01f);
	const int steps = (int)CLAMP((to_s - from_s) / step_s, 1.0f, 40.0f);

	Vector2 prev = to_screen(t.pos);
	bool have_prev = true;
	for(int i = 1; i <= steps; i++) {
		const float at = from_s + (to_s - from_s) * ((float)i / (float)steps);
		const float u = at / dur;
		const float e = ease_value(t.ease, u);
		Vector2 p = t.start_pos + t.move_dir * (t.move_speed * dur * e);
		// Beyond the cursor circle a target is not reachable, so the preview
		// stops there instead of running off the screen.
		const float len = p.length();
		bool clipped = false;
		if(len > reach - t.radius) {
			p = p.normalized() * MAX(reach - t.radius, 1.0f);
			clipped = true;
		}
		const Vector2 sp = to_screen(p);
		// Local slope of the ease curve, normalised, shades the segment by speed.
		const float h = 0.01f;
		const float a = ease_value(t.ease, MAX(0.0f, u - h));
		const float b = ease_value(t.ease, MIN(1.0f, u + h));
		const float slope = (b - a) / (2.0f * h);
		const float intensity = CLAMP(slope, 0.0f, 1.5f) / 1.5f;
		// Fade along the preview and kill it at the clip point.
		const float along = (float)i / (float)steps;
		const float fade = (1.0f - along) * (clipped ? 0.0f : 1.0f);
		const Color col = Color(0.35f + 0.6f * intensity, 0.75f + 0.25f * intensity, 1.0f,
				(0.10f + 0.35f * intensity) * fade);
		if(have_prev && fade > 0.01f) {
			draw_line(prev, sp, col, 2.0f, true);
		}
		prev = sp;
		have_prev = true;
	}
}

// ---------------------------------------------------------------------------
// The laser
// ---------------------------------------------------------------------------
//
// Three stacked segments fake a hot core inside a wide, soft glow: an outer
// translucent ribbon, a mid ribbon and an almost-white centre. A sweeping
// "shot" animation stretches from the muzzle to the cursor whenever the beam
// fires, and a rotating reticle brackets whatever silhouette the beam crosses.
void AimTrainer::draw_laser() {
	if(screen != Screen::GAME) {
		return;
	}

	const Vector2 origin = screen_center;
	const Vector2 tip = laser_end;
	const Vector2 delta = tip - origin;
	const float len = delta.length();
	if(len < 1.0f) {
		return;
	}
	const Vector2 d = delta / len;
	const Vector2 edge = origin + d * laser_max_reach();

	const Color hot = laser_lock ? Color(1.0f, 0.42f, 0.34f) : Color(0.42f, 0.88f, 1.0f);
	const float shimmer = 0.86f + 0.14f * sin(effects_clock * 22.0f);
	const float fire = fire_flash;

	// Outer glow: a translucent wedge that tapers toward the tip.
	const float wide = 11.0f + 9.0f * fire;
	{
		PackedVector2Array pts;
		PackedColorArray cols;
		pts.push_back(origin + Vector2(-d.y, d.x) * wide);
		pts.push_back(edge + Vector2(-d.y, d.x) * wide * 0.25f);
		pts.push_back(edge + Vector2(d.y, -d.x) * wide * 0.25f);
		pts.push_back(origin + Vector2(d.y, -d.x) * wide);
		Color a = hot;
		a.a = (0.09f + 0.10f * fire) * shimmer;
		Color b = hot;
		b.a = 0.0f;
		cols.push_back(a);
		cols.push_back(b);
		cols.push_back(b);
		cols.push_back(a);
		draw_polygon(pts, cols);
	}

	// Beam ribbons.
	draw_line(origin, edge, Color(hot.r, hot.g, hot.b, (0.16f + 0.22f * fire) * shimmer), 8.0f + 5.0f * fire, true);
	draw_line(origin, edge, Color(hot.r, hot.g, hot.b, 0.42f + 0.25f * fire), 3.4f + 1.6f * fire, true);
	draw_line(origin, edge, Color(1.0f, 1.0f, 1.0f, 0.80f + 0.20f * fire), 1.4f + 0.8f * fire, true);

	// Travelling dash highlights inside the beam.
	const int dashes = 7;
	for(int i = 0; i < dashes; i++) {
		const float phase = fmodf(effects_clock * 1.15f + (float)i / (float)dashes, 1.0f);
		const float seg = len * 0.06f;
		const float centre = phase * len;
		const Vector2 a = origin + d * MAX(0.0f, centre - seg);
		const Vector2 b = origin + d * MIN(len, centre + seg);
		if(b.x == a.x && b.y == a.y) {
			continue;
		}
		const float fade = 1.0f - phase;
		draw_line(a, b, Color(1.0f, 1.0f, 1.0f, 0.14f * fade), 2.2f, true);
	}

	// Muzzle: concentric bloom plus a short recoil streak.
	draw_bloom(this, origin, 26.0f + 20.0f * fire, Color(hot.r, hot.g, hot.b, 0.28f + 0.30f * fire), 8);
	draw_circle(origin, 4.5f + 3.0f * fire, Color(1, 1, 1, 0.95f));
	{
		const Vector2 back = origin - d * (10.0f + 16.0f * fire);
		draw_line(origin, back, Color(1.0f, 1.0f, 1.0f, 0.55f * (0.35f + fire)), 2.0f, true);
	}

	// Shot sweep: a bright packet racing from the muzzle to the cursor.
	if(fire > 0.01f) {
		const float k = 1.0f - fire;
		const float at = len * k;
		const Vector2 s0 = origin + d * MAX(0.0f, at - 34.0f);
		const Vector2 s1 = origin + d * MIN(len, at + 10.0f);
		draw_line(s0, s1, Color(1.0f, 1.0f, 1.0f, 0.85f * fire), 4.0f, true);
	}

	// Impact point: a hot flare everywhere the beam ends.
	draw_bloom(this, tip, 15.0f + 12.0f * fire, Color(hot.r, hot.g, hot.b, 0.42f + 0.30f * fire), 8);

	if(laser_lock) {
		// Locked-on silhouette: pulsing double ring and four rotating brackets.
		const float pr = 12.0f + 2.0f * sin(effects_clock * 6.0f);
		draw_arc(tip, pr, 0, Math_TAU, 28, Color(1.0f, 0.50f, 0.42f, 0.75f), 1.8f, true);
		draw_arc(tip, pr + 6.0f, 0, Math_TAU, 32, Color(1.0f, 0.62f, 0.52f, 0.32f), 1.2f, true);
		for(int i = 0; i < 4; i++) {
			const float base = effects_clock * 1.6f + (float)i * Math_PI * 0.5f;
			const float rr = 19.0f;
			const Vector2 a = tip + Vector2(cos(base), sin(base)) * rr;
			const Vector2 b = tip + Vector2(cos(base + 0.34f), sin(base + 0.34f)) * rr;
			draw_line(a, b, Color(1.0f, 0.45f, 0.36f, 0.9f), 2.4f, true);
		}
	}

	// Sparks streaking down the beam.
	for(int i = 0; i < sparks.size(); i++) {
		const Spark &s = sparks[i];
		const float k = 1.0f - s.age / MAX(s.life, 0.0001f);
		const Color col = laser_lock ? Color(1.0f, 0.62f, 0.42f, k) : Color(0.72f, 0.92f, 1.0f, k);
		draw_circle(s.pos, s.size * (0.4f + 0.6f * k), col);
	}
}

// Impact / miss feedback: expanding rings, ejected markers and floating score
// numbers. Drawn after the targets so the feedback always reads on top.
void AimTrainer::draw_effects() {
	Ref<Font> font = ThemeDB::get_singleton()->get_default_theme()->get_font("font", "Label");

	for(int i = 0; i < rings.size(); i++) {
		const Ring &r = rings[i];
		const float k = CLAMP(r.age / MAX(r.life, 0.0001f), 0.0f, 1.0f);
		const float ease = 1.0f - (1.0f - k) * (1.0f - k);
		const float rad = r.from_r + (r.to_r - r.from_r) * ease;
		Color c = r.col;
		c.a *= (1.0f - k) * (1.0f - k);
		draw_arc(r.pos, rad, 0, Math_TAU, 40, c, r.width * (1.0f - 0.6f * k), true);
	}

	for(int i = 0; i < hit_markers.size(); i++) {
		const HitMarker &h = hit_markers[i];
		const float k = CLAMP(h.age / MAX(h.life, 0.0001f), 0.0f, 1.0f);
		const float a = 1.0f - k;
		const float sz = h.size * (0.55f + 0.75f * k);
		const Color col = h.miss ? Color(1.0f, 0.35f, 0.32f, a) : Color(0.85f, 1.0f, 0.90f, a);
		const float gap = sz * 0.30f;
		draw_line(h.pos + Vector2(-sz, -sz), h.pos + Vector2(-gap, -gap), col, 2.2f, true);
		draw_line(h.pos + Vector2(gap, gap), h.pos + Vector2(sz, sz), col, 2.2f, true);
		draw_line(h.pos + Vector2(-sz, sz), h.pos + Vector2(-gap, gap), col, 2.2f, true);
		draw_line(h.pos + Vector2(gap, -gap), h.pos + Vector2(sz, -sz), col, 2.2f, true);
	}

	for(int i = 0; i < pops.size(); i++) {
		const Pop &p = pops[i];
		const float k = CLAMP(p.age / 0.9f, 0.0f, 1.0f);
		const float fade = 1.0f - k;
		const float rad = 10.0f + k * 34.0f;
		const Color col = p.miss ? Color(1.0f, 0.28f, 0.28f) : Color(0.35f, 1.0f, 0.45f);
		Color c = col;
		c.a = fade;
		draw_arc(p.pos, rad, 0, Math_TAU, 32, c, 2.5f * fade + 0.5f, true);
		if(!p.miss) {
			// Second, faster ring for hits.
			draw_arc(p.pos, rad * 1.55f, 0, Math_TAU, 32, Color(col.r, col.g, col.b, fade * 0.35f), 1.4f, true);
		}
		if(p.score > 0) {
			draw_string(font, p.pos + Vector2(-30.0f, -14.0f - k * 34.0f), "+" + String::num(p.score),
					HORIZONTAL_ALIGNMENT_CENTER, 60, 17,
					Color(0.65f, 1.0f, 0.72f, fade));
		}
	}
}

void AimTrainer::draw_hud() {
	Ref<Font> font = ThemeDB::get_singleton()->get_default_theme()->get_font("font", "Label");
	const Vector2 size = get_viewport_rect().size;

	// Score / accuracy panel.
	draw_rect(Rect2(Vector2(16, 16), Vector2(330, 96)), Color(0.08f, 0.08f, 0.10f, 0.72f), true);
	draw_rect(Rect2(Vector2(16, 16), Vector2(330, 96)), Color(0.45f, 0.65f, 0.95f, 0.25f), false, 1.0f);
	draw_string(font, Vector2(30, 44), "SCORE: " + String::num(stats.score_total), HORIZONTAL_ALIGNMENT_LEFT, -1, 22, Color(1, 1, 1));
	draw_string(font, Vector2(30, 70), "Accuracy: " + String::num(stats.current_accuracy, 1) + "%  (" + String::num(stats.hits) + "/" + String::num(stats.total_interactions) + ")",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 16, Color(0.8f, 0.85f, 0.95f));
	draw_string(font, Vector2(30, 96), "Gray: " + String::num(stats.score_gray) + "   White: " + String::num(stats.score_white) +
					"   shots: " + String::num(stats.total_interactions) +
					"   bare: " + String::num(stats.bare_shots),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color(0.7f, 0.75f, 0.8f));

	// Live accuracy bar under the panel.
	{
		const float w = 330.0f;
		const float frac = CLAMP(stats.current_accuracy / 100.0f, 0.0f, 1.0f);
		draw_rect(Rect2(Vector2(16, 116), Vector2(w, 6)), Color(1, 1, 1, 0.08f), true);
		const Color bar = Color(0.35f, 0.95f, 0.45f).lerp(Color(0.95f, 0.35f, 0.35f), 1.0f - frac);
		draw_rect(Rect2(Vector2(16, 116), Vector2(w * frac, 6)), bar, true);
	}

	// Cursor reach circle (cl_mouse_max_distance emulation).
	const float radius = input.get_effective_max_distance() * px_per_unit;
	draw_arc(screen_center, radius, 0, Math_TAU, 128, Color(1, 1, 1, 0.10f), 1.5f, true);
	// Ticks every 45 degrees make the reach boundary readable while aiming.
	for(int i = 0; i < 8; i++) {
		const float a = (float)i * Math_PI * 0.25f;
		const Vector2 dir = Vector2(cos(a), sin(a));
		draw_line(screen_center + dir * (radius - 7.0f), screen_center + dir * (radius + 7.0f),
				Color(1, 1, 1, 0.13f), 1.2f, true);
	}

	// Player point.
	draw_circle(screen_center, 3.0f, Color(1, 1, 1, 0.35f));

	// Crosshair at the cursor position, brighter while the beam is on a target.
	const Vector2 cur = to_screen(input.pos);
	const Color ch = laser_lock ? Color(1.0f, 0.55f, 0.45f, 0.95f) : Color(1, 0.3f, 0.3f, 0.9f);
	const float ch_len = laser_lock ? 12.0f : 9.0f;
	draw_line(cur - Vector2(ch_len, 0), cur + Vector2(ch_len, 0), ch, 1.5f);
	draw_line(cur - Vector2(0, ch_len), cur + Vector2(0, ch_len), ch, 1.5f);
	draw_circle(cur, 1.6f, Color(1, 1, 1, 0.8f));

	// Last score popup.
	if(last_score > 0) {
		draw_string(font, Vector2(size.x * 0.5f - 60, size.y - 40), "+" + String::num(last_score),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 24, Color(0.5f, 1.0f, 0.6f, 0.9f));
	}
	draw_string(font, Vector2(16, size.y - 20), "LMB - shoot white    RMB - hold gray    ESC - results    TAB - settings    R - reset",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color(0.6f, 0.6f, 0.65f));
}

void AimTrainer::draw_results() {
	Ref<Font> font = ThemeDB::get_singleton()->get_default_theme()->get_font("font", "Label");
	const Vector2 size = get_viewport_rect().size;

	draw_rect(Rect2(Vector2(0, 0), size), Color(0.04f, 0.04f, 0.06f, 0.90f), true);

	const float x = 90.0f;
	float y = 90.0f;
	draw_string(font, Vector2(x, y), "RUN RESULTS", HORIZONTAL_ALIGNMENT_LEFT, -1, 30, Color(1, 1, 1));
	y += 50.0f;

	auto line = [&](const String &s, int sz, Color col) {
		draw_string(font, Vector2(x, y), s, HORIZONTAL_ALIGNMENT_LEFT, -1, sz, col);
		y += sz + 12.0f;
	};

	line("Score: " + String::num(stats.score_total) + "   (gray " + String::num(stats.score_gray) +
					" / white " + String::num(stats.score_white) + ")",
			18, Color(0.85f, 0.9f, 1.0f));
	line("Shots: " + String::num(stats.total_interactions) + "   hits: " + String::num(stats.hits) +
					"   misses: " + String::num(stats.misses) +
					"   (of which bare: " + String::num(stats.bare_shots) + ")",
			16, Color(0.8f, 0.8f, 0.85f));
	y += 14.0f;

	draw_string(font, Vector2(x, y), "a. Hold deviation (RMB)", HORIZONTAL_ALIGNMENT_LEFT, -1, 17, Color(0.6f, 0.85f, 1.0f));
	y += 26.0f;
	line("   max: " + String::num(stats.hold_dev_max(), 3) + " s   avg: " + String::num(stats.hold_dev_avg(), 3) +
					" s   min: " + String::num(stats.hold_dev_min(), 3) + " s   samples: " + String::num(stats.hold_deviations.size()),
			15, Color(0.8f, 0.85f, 0.9f));
	y += 10.0f;

	draw_string(font, Vector2(x, y), "b. Time between shots", HORIZONTAL_ALIGNMENT_LEFT, -1, 17, Color(0.6f, 0.85f, 1.0f));
	y += 26.0f;
	line("   avg: " + String::num(stats.shot_interval_avg(), 3) + " s   min: " + String::num(stats.shot_interval_min(), 3) +
					" s   max: " + String::num(stats.shot_interval_max(), 3) + " s   spread: " + String::num(stats.shot_interval_std(), 3) + " s",
			15, Color(0.8f, 0.85f, 0.9f));
	y += 10.0f;

	draw_string(font, Vector2(x, y), "c. Misses (last 8)", HORIZONTAL_ALIGNMENT_LEFT, -1, 17, Color(0.6f, 0.85f, 1.0f));
	y += 26.0f;
	const int from = MAX(0, stats.miss_records.size() - 8);
	for(int i = from; i < stats.miss_records.size(); i++) {
		const MissRecord &m = stats.miss_records[i];
		if(m.nearest_distance < 0.0f || !m.had_target_nearby) {
			line("   no target on the beam (bare shot)", 14, Color(0.75f, 0.7f, 0.7f));
			continue;
		}
		line("   deviation: " + String::num(m.deviation_pct, 1) + "%   vector: (" + String::num(m.error_vector.x, 0) + ", " +
						String::num(m.error_vector.y, 0) + ")  " + (m.overshoot ? "OVERSHOOT" : "UNDERSHOOT"),
				14, Color(0.95f, 0.6f, 0.55f));
	}
	y += 10.0f;

	draw_string(font, Vector2(x, y), "d. Accuracy", HORIZONTAL_ALIGNMENT_LEFT, -1, 17, Color(0.6f, 0.85f, 1.0f));
	y += 26.0f;
	line("   peak: " + String::num(stats.accuracy_peak(), 1) + "%   avg: " + String::num(stats.accuracy_average(), 1) +
					"%   min: " + String::num(stats.accuracy_min(), 1) + "%",
			15, Color(0.8f, 0.85f, 0.9f));
	y += 20.0f;

	draw_string(font, Vector2(x, y), "ENTER / ESC - start again", HORIZONTAL_ALIGNMENT_LEFT, -1, 18, Color(1.0f, 0.9f, 0.5f));
}

void AimTrainer::draw_menu() {
	Ref<Font> font = ThemeDB::get_singleton()->get_default_theme()->get_font("font", "Label");
	const Vector2 size = get_viewport_rect().size;

	draw_rect(Rect2(Vector2(0, 0), size), Color(0.04f, 0.05f, 0.07f, 0.93f), true);
	// Heading sits directly above the first row of the menu block.
	float panel_x0 = size.x * 0.5f;
	float panel_x1 = size.x * 0.5f;
	for(int i = 0; i < buttons.size(); i++) {
		panel_x0 = MIN(panel_x0, buttons[i].rect.position.x);
		panel_x1 = MAX(panel_x1, buttons[i].rect.position.x + buttons[i].rect.size.x);
	}
	const float title_y = menu_top - 34.0f;
	draw_string(font, Vector2(panel_x0, title_y), "SETTINGS",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 28, Color(1, 1, 1));
	// A rule under the title frames the panel.
	draw_line(Vector2(panel_x0, title_y + 10.0f), Vector2(panel_x1, title_y + 10.0f),
			Color(0.45f, 0.65f, 0.95f, 0.35f), 1.5f);

	// Section headings, each with a rule running to its own column's edge.
	for(int i = 0; i < section_labels.size(); i++) {
		const SectionLabel &sl = section_labels[i];
		draw_string(font, sl.pos, sl.text, HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color(0.45f, 0.65f, 0.95f, 0.95f));
		const float tw = font->get_string_size(sl.text, HORIZONTAL_ALIGNMENT_LEFT, -1, 14).x;
		const float rule_end = MAX(sl.rule_end, sl.pos.x + tw + 12.0f);
		draw_line(Vector2(sl.pos.x + tw + 12.0f, sl.pos.y - 4.0f), Vector2(rule_end, sl.pos.y - 4.0f),
				Color(0.45f, 0.65f, 0.95f, 0.18f), 1.0f);
	}

	for(int i = 0; i < buttons.size(); i++) {
		const Button &b = buttons[i];
		const bool sel = (i == sel_row);
		draw_rect(b.rect, sel ? Color(0.16f, 0.20f, 0.30f, 0.95f) : Color(0.10f, 0.11f, 0.14f, 0.85f), true);
		if(sel) {
			draw_rect(b.rect, Color(0.45f, 0.65f, 0.95f, 0.9f), false, 2.0f);
			// Selection marker: a glowing notch on the left edge.
			draw_rect(Rect2(b.rect.position, Vector2(4.0f, b.rect.size.y)), Color(0.55f, 0.80f, 1.0f, 0.95f), true);
		}
		// Baseline sits inside the bar. These are absolute canvas coordinates;
		// adding b.rect.position on top of an already-absolute y pushed the
		// text one bar-height below its own row.
		const float ty = b.rect.position.y + b.rect.size.y * 0.5f + 6.0f;
		draw_string(font, Vector2(b.rect.position.x + 18.0f, ty), b.label, HORIZONTAL_ALIGNMENT_LEFT, -1, 16,
				sel ? Color(1, 1, 1) : Color(0.72f, 0.74f, 0.80f));
		const bool is_editing = editing && i == sel_row;
		if(b.value != "" || is_editing) {
			String v = "< " + b.value + " >";
			if(is_editing) {
				v = edit_buffer.is_empty() ? "< _ >" : "< " + edit_buffer + " >";
			}
			// Colour rows get a swatch next to the hex so the choice is visible.
			float value_x = b.rect.position.x + b.rect.size.x - 18.0f;
			if(row_edits_color(b.action)) {
				const Color swatch = b.action == ACT_COLOR_GRAY ? tuning.color_gray : tuning.color_white;
				const float sw = 22.0f;
				value_x -= sw + 10.0f;
				const Rect2 box(Vector2(value_x, b.rect.position.y + (b.rect.size.y - sw) * 0.5f), Vector2(sw, sw));
				draw_rect(box, swatch, true);
				draw_rect(box, Color(1, 1, 1, 0.35f), false, 1.0f);
				value_x -= 8.0f;
			}
			// Right-align the value so every row's number lines up, instead of
			// drifting with the label width.
			const float vw = font->get_string_size(v, HORIZONTAL_ALIGNMENT_LEFT, -1, 16).x;
			draw_string(font, Vector2(value_x - vw, ty), v,
					HORIZONTAL_ALIGNMENT_LEFT, -1, 16,
					is_editing ? Color(1.0f, 0.85f, 0.35f) : Color(0.65f, 0.85f, 1.0f));
		}
	}

	// Contextual hint: what the selected row accepts.
	float bottom = 0.0f;
	for(int i = 0; i < buttons.size(); i++) {
		bottom = MAX(bottom, buttons[i].rect.position.y + buttons[i].rect.size.y);
	}
	const float hy = (buttons.is_empty() ? size.y * 0.5f : bottom) + 30.0f;
	String hint = "W/S or arrows - select   A/D - change   ENTER - back";
	if(sel_row >= 0 && sel_row < buttons.size()) {
		const int act = buttons[sel_row].action;
		if(act == ACT_BACK) {
			hint = "ENTER - return to the game";
		} else if(row_edits_color(act)) {
			hint = "A/D - brighten or darken   ENTER - type a hex colour (e.g. #44A2FF)   ESC - back";
		} else if(row_is_editable(act)) {
			String unit = "";
			switch(act) {
				case ACT_SPAWN:
				case ACT_HOLD:
				case ACT_HOLD_MIN:
					unit = "  (in hundredths of a second)";
					break;
				case ACT_TOLERANCE:
					unit = "  (percent of the start-of-run hold)";
					break;
				case ACT_GRAY_CHANCE:
					unit = "  (percent)";
					break;
				default:
					break;
			}
			hint = "A/D or arrows - change   ENTER - type a value" + unit + "   ESC - back";
		} else {
			hint = "A/D or arrows - change   ENTER - apply   ESC - back";
		}
	}
	draw_string(font, Vector2((size.x - 800.0f) * 0.5f, hy), hint,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 15, Color(0.6f, 0.62f, 0.68f));
}

void AimTrainer::_draw() {
	// Background: dark field with a soft radial lift toward the centre so the
	// play area reads as a lit disc.
	const Vector2 vs = get_viewport_rect().size;
	draw_rect(Rect2(Vector2(), vs), Color(0.055f, 0.06f, 0.078f));
	const int glow_steps = 26;
	const float glow_r = MIN(vs.x, vs.y) * 0.62f;
	for(int i = glow_steps; i >= 1; i--) {
		const float k = (float)i / (float)glow_steps;
		const float a = 0.016f * (1.0f - k) + 0.004f;
		draw_circle(screen_center, glow_r * k, Color(0.42f, 0.62f, 0.95f, a));
	}

	// Faint concentric range rings around the player point.
	for(int i = 1; i <= 4; i++) {
		const float rr = input.get_effective_max_distance() * px_per_unit * (float)i / 4.0f;
		draw_arc(screen_center, rr, 0, Math_TAU, 96, Color(0.55f, 0.75f, 1.0f, 0.045f), 1.0f, true);
	}

	// Rotating radar sweep: a slow conic wedge that keeps the field alive.
	{
		const float sweep_r = MIN(vs.x, vs.y) * 0.70f;
		const float base = effects_clock * 0.35f;
		const int steps = 16;
		for(int i = 0; i < steps; i++) {
			const float a0 = base + (float)i / (float)steps * 0.55f;
			const float a1 = base + (float)(i + 1) / (float)steps * 0.55f;
			const float fade = 1.0f - (float)i / (float)steps;
			PackedVector2Array pts;
			pts.push_back(screen_center);
			pts.push_back(screen_center + Vector2(cos(a0), sin(a0)) * sweep_r);
			pts.push_back(screen_center + Vector2(cos(a1), sin(a1)) * sweep_r);
			draw_colored_polygon(pts, Color(0.45f, 0.70f, 1.0f, 0.016f * fade));
		}
	}

	// Ambient dust.
	for(int i = 0; i < motes.size(); i++) {
		const Mote &m = motes[i];
		const float tw = 0.25f + 0.20f * sin(m.phase * 1.7f);
		draw_circle(m.pos, m.size, Color(0.65f, 0.78f, 1.0f, tw));
	}

	// Corner vignette: a ring of dark wedges from mid-screen to past the corners
	// so the frame settles down and the lit centre keeps the eye.
	{
		const float inner = MIN(vs.x, vs.y) * 0.34f;
		const float outer = Vector2(vs).length() * 0.62f;
		const int segs = 40;
		const Vector2 vc = vs * 0.5f;
		for(int i = 0; i < segs; i++) {
			const float a0 = (float)i / (float)segs * Math_TAU;
			const float a1 = (float)(i + 1) / (float)segs * Math_TAU;
			PackedVector2Array pts;
			pts.push_back(vc + Vector2(cos(a0), sin(a0)) * inner);
			pts.push_back(vc + Vector2(cos(a0), sin(a0)) * outer);
			pts.push_back(vc + Vector2(cos(a1), sin(a1)) * outer);
			pts.push_back(vc + Vector2(cos(a1), sin(a1)) * inner);
			PackedColorArray cols;
			Color none = Color(0, 0, 0, 0);
			Color dark = Color(0.01f, 0.012f, 0.02f, 0.42f);
			cols.push_back(none);
			cols.push_back(dark);
			cols.push_back(dark);
			cols.push_back(none);
			draw_polygon(pts, cols);
		}
	}

	// Everything below this point is gameplay and participates in screen shake.
	draw_set_transform(shake_offset, 0.0f, Vector2(1, 1));

	// Shatter fragments: shrink and fade as they dissipate.
	for(int i = 0; i < debris.size(); i++) {
		const Debris &d = debris[i];
		const float k = 1.0f - d.age / MAX(d.life, 0.0001f);
		const float a = k * k;
		const float sz = d.size * (0.35f + 0.65f * k);
		draw_set_transform(shake_offset + d.pos, d.angle, Vector2(1, 1));
		{
			// Shards carry the target's own colour, cooled towards its lighter
			// shade as they fade, so a custom palette reads in the debris too.
			const Color c = base_color(d.gray);
			const Color cooled = toward_white(c, 0.45f * (1.0f - k));
			Color shard = cooled;
			shard.a = a;
			draw_rect(Rect2(-Vector2(sz, sz) * 0.5f, Vector2(sz, sz)), shard, true);
			Color edge = toward_white(c, 0.7f);
			edge.a = a * 0.6f;
			draw_rect(Rect2(-Vector2(sz, sz) * 0.5f, Vector2(sz, sz)), edge, false, 1.0f);
		}
		draw_set_transform(shake_offset, 0.0f, Vector2(1, 1));
	}

	for(int i = 0; i < targets.size(); i++) {
		if(!targets[i].active) {
			continue;
		}
		draw_trajectory(targets[i]);
	}

	draw_laser();

	for(int i = 0; i < targets.size(); i++) {
		if(!targets[i].active) {
			continue;
		}
		draw_target(targets[i]);
	}

	draw_effects();

	// The game HUD belongs to the field only: behind the settings and results
	// overlays it just bleeds through the panel.
	if(screen == Screen::GAME) {
		draw_hud();
	}

	// Overlays ignore the shake so menus stay rock steady.
	draw_set_transform(Vector2(), 0.0f, Vector2(1, 1));
	if(screen == Screen::RESULTS) {
		draw_results();
	} else if(screen == Screen::SETTINGS) {
		draw_menu();
	}
}
