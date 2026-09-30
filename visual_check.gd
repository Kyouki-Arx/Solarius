extends SceneTree

# Verification harness for the aim trainer. Drives the production input path and
# writes PNGs of each state to user:// so the visuals can be inspected too.
#
# Run with:
#   tools/godot.exe --path . --script res://visual_check.gd
#
# Tests that force an aim in one step call debug_freeze_motion(true): without it
# a moving target drifts between the forced aim and the check, which no human
# player experiences, and assertions become flaky.

const OUT := "user://"
const CURSOR_MAX := 400.0
const CFG := "aimtrainer.cfg"

var fails := 0
var checks := 0
var _elapsed := 0.0
var _watchdog_fired := false
# The AimTrainer instance under test, so helpers can query its live state.
var _game = null
# The player's own config, stashed on start and put back when the run ends.
var _cfg_backup := ""
var _had_cfg := false

func _initialize() -> void:
	# Deterministic baseline: the suite runs from defaults, not from whatever a
	# previous run (or the player) left in the config. The existing file is
	# stashed first and restored on the way out, because the menu writes the
	# config on every change and would otherwise clobber the player's settings.
	var path := OS.get_user_data_dir() + "/" + CFG
	if FileAccess.file_exists(path):
		var f := FileAccess.open(path, FileAccess.READ)
		if f != null:
			_cfg_backup = f.get_as_text()
			_had_cfg = true
			f.close()
	DirAccess.remove_absolute(path)
	var scene: PackedScene = load("res://main.tscn")
	_game = scene.instantiate()
	root.add_child(_game)
	_run(_game)

# Puts the player's config back and exits. Called on every path out, so an
# aborted run still leaves the settings alone.
func _finish() -> void:
	var path := OS.get_user_data_dir() + "/" + CFG
	if _had_cfg:
		var f := FileAccess.open(path, FileAccess.WRITE)
		if f != null:
			f.store_string(_cfg_backup)
			f.close()
		print("(player config restored)")
	else:
		DirAccess.remove_absolute(path)
		print("(no config existed before the run; left none behind)")
	quit()

# Wall-clock guard so a stalled await can never hang the harness forever.
func _process(delta: float) -> bool:
	_elapsed += delta
	if _elapsed > 120.0 and not _watchdog_fired:
		_watchdog_fired = true
		print("!! WATCHDOG: harness exceeded 120 s of engine time, aborting")
		_finish()
	return false

func _frames(n: int) -> void:
	for i in n:
		await process_frame

# Wall-clock wait: robust even if the frame rate collapses.
func _idle(seconds: float) -> void:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < deadline:
		await process_frame

# Waits (in wall-clock time, so the frame rate cannot skew it) until no target
# is left, up to `seconds`. Returns true when the field actually emptied.
func _wait_until_empty(seconds: float) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if _game.debug_active() == 0:
			return true
	return false

func _ok(cond: bool, label: String, detail: String = "") -> void:
	checks += 1
	if cond:
		print("  PASS  ", label, "  ", detail)
	else:
		fails += 1
		print("  FAIL  ", label, "  ", detail)

# Menu row index looked up by label, so tests do not break when rows are added.
func _row(label: String) -> int:
	var rows: PackedStringArray = _game.debug_menu_rows()
	for i in rows.size():
		if rows[i] == label:
			return i
	push_error("menu row not found: " + label)
	return -1

func _select(label: String) -> void:
	_game.debug_menu_select(_row(label))

func _save(name: String) -> void:
	# Let effects settle so the frame is not a half-faded transient.
	await _frames(3)
	# Under --headless there is no rendering device at all, and the viewport is
	# 1x1. Skip instead of spamming texture errors; the assertion run itself is
	# only meaningful with a real window anyway (the playfield is window-sized).
	if DisplayServer.get_name() == "headless":
		return
	var img: Image = root.get_texture().get_image()
	if img == null:
		return
	img.save_png(OUT + name + ".png")

func _press(button: int, pressed: bool) -> void:
	var ev := InputEventMouseButton.new()
	ev.button_index = button
	ev.pressed = pressed
	root.push_input(ev)

func _key(code: int) -> void:
	var ev := InputEventKey.new()
	ev.keycode = code
	ev.pressed = true
	root.push_input(ev)

func _press_release(button: int) -> void:
	_press(button, true)
	await _frames(2)
	_press(button, false)
	await _frames(2)

func _run(game) -> void:
	await _frames(12)
	game.debug_freeze_motion(true)

	print("=== 1. pacing ===")
	var n: int = game.get_target_count()
	_ok(n == 3, "3 targets alive at spawn", "got %d" % n)
	var tune: Dictionary = game.debug_tuning()
	_ok(tune["max_size"] <= 50.0, "max target size is modest", "%.0f px" % tune["max_size"])
	_ok(tune["min_size"] < tune["max_size"], "size band is coherent",
			"%.0f..%.0f" % [tune["min_size"], tune["max_size"]])

	print("=== 2. beam follows the cursor ===")
	for d in [Vector2(1, 0), Vector2(0, -1), Vector2(-1, 0), Vector2(0, 1), Vector2(1, 1)]:
		game.debug_set_cursor(d.normalized() * CURSOR_MAX)
		await process_frame
		var dot: float = game.laser_tip_dir().normalized().dot(game.get_cursor_pos().normalized())
		_ok(dot > 0.999, "beam aligns with cursor %s" % d, "dot=%.4f" % dot)

	print("=== 3. cursor clamp ===")
	game.debug_force_move_cursor(Vector2(5000, 5000))
	await process_frame
	var cl: float = game.get_cursor_pos().length()
	_ok(cl <= CURSOR_MAX + 1.0, "cursor clamped to cl_mouse_max_distance", "|cursor|=%.2f" % cl)

	print("=== 4. targets fill the whole visible playfield ===")
	# Spawning is uniform over the field, not a ring around the player point.
	var area: Rect2 = game.debug_spawn_area()
	var pr: float = tune["playfield_radius"]
	print("  spawn area=", area, " playfield radius=%.0f cursor=%.0f" % [pr, tune["mouse_max_distance"]])
	_ok(area.size.x > tune["mouse_max_distance"] && area.size.y > tune["mouse_max_distance"],
			"spawn area is much wider than the cursor circle",
			"%.0fx%.0f vs cursor %.0f" % [area.size.x, area.size.y, tune["mouse_max_distance"]])
	var reach: float = game.debug_spawn_reach()
	# The corner radius (radius * sqrt(2)) is larger than the half-height, which
	# is what an area spans rather than a ring.
	_ok(reach > tune["mouse_max_distance"] * 1.3, "targets can sit far beyond the cursor circle",
			"reach=%.0f vs cursor %.0f" % [reach, tune["mouse_max_distance"]])
	# The beam must be able to reach the far corner, or a target could appear
	# somewhere it can never be hit.
	_ok(pr + 100.0 <= 1200.0, "beam covers the playfield diagonal",
			"need >= %.0f, beam limit >= %.0f" % [pr, 1200.0])

	# Sampling check: spawn a crowd and confirm points land on both axes and
	# outside the old ring, in every direction.
	var min_x := 1e9
	var max_x := -1e9
	var min_y := 1e9
	var max_y := -1e9
	var outside := 0
	var samples := 0
	for i in 300:
		game.debug_clear_targets()
		game.debug_spawn_target()
		await process_frame
		if game.debug_active() == 0:
			continue
		var p: Vector2 = game.first_target_pos()
		samples += 1
		min_x = minf(min_x, p.x)
		max_x = maxf(max_x, p.x)
		min_y = minf(min_y, p.y)
		max_y = maxf(max_y, p.y)
		if p.length() > tune["mouse_max_distance"]:
			outside += 1
	print("  sampled %d points: x %.0f..%.0f  y %.0f..%.0f  outside circle: %d" % [
			samples, min_x, max_x, min_y, max_y, outside])
	_ok(max_x > area.size.x * 0.35 && min_x < -area.size.x * 0.35,
			"points span the full width", "x %.0f..%.0f" % [min_x, max_x])
	_ok(max_y > area.size.y * 0.30 && min_y < -area.size.y * 0.30,
			"points span the full height", "y %.0f..%.0f" % [min_y, max_y])
	_ok(outside > samples / 5, "a solid share of targets sit outside the cursor circle",
			"%d of %d (%.0f%%)" % [outside, samples, 100.0 * outside / maxf(samples, 1)])
	_ok(not game.debug_any_target_unreachable(), "nothing spawns outside the visible field")

	# Refill a normal field for the screenshot.
	game.debug_clear_targets()
	for i in 3:
		game.debug_spawn_target()
	await _frames(3)
	await _save("v_outside")

	print("=== 5. white target: aim and shoot ===")
	game.debug_clear_targets()
	game.debug_spawn_color(false)
	await _frames(2)
	_ok(game.debug_active() == 1 and not game.debug_is_gray(), "one white target spawned")
	var wpos: Vector2 = game.first_target_pos()
	var wrad: float = game.first_target_radius()
	print("  white target pos=", wpos, " |pos|=%.1f r=%.1f" % [wpos.length(), wrad])
	game.debug_set_cursor(wpos)
	await _frames(3)
	_ok(game.is_laser_locked(), "beam locks the target it crosses")
	await _idle(0.3)
	await _save("v_beam_lock_white")
	var hits0: int = game.get_hit_count()
	var score0: int = game.get_score()
	game.debug_click(game.get_cursor_pos(), true)
	await _frames(3)
	print("  after shot: hits %d -> %d, score %d -> %d, active=%d" % [
			hits0, game.get_hit_count(), score0, game.get_score(), game.debug_active()])
	_ok(game.get_hit_count() == hits0 + 1, "LMB on a white target scores",
			"score=%d" % game.get_score())
	await _save("v_hit_white")

	print("=== 6. bare shots count against accuracy ===")
	# Accuracy is hits over every shot fired, so a shot into open space is a miss
	# exactly like a shot that missed a target.
	var shots0: int = game.get_shot_count()
	var miss0: int = game.get_miss_count()
	var bare0: int = game.get_bare_shot_count()
	var acc0: float = game.get_accuracy()
	var hits_before_bare: int = game.get_hit_count()
	game.debug_set_cursor(Vector2(0, -CURSOR_MAX))
	await _frames(2)
	_ok(not game.is_laser_locked(), "empty beam reports no lock")
	game.debug_click(game.get_cursor_pos(), true)
	await _frames(3)
	_ok(game.get_shot_count() == shots0 + 1, "the shot is counted",
			"shots %d -> %d" % [shots0, game.get_shot_count()])
	_ok(game.get_miss_count() == miss0 + 1, "firing into open space counts as a miss",
			"misses %d -> %d" % [miss0, game.get_miss_count()])
	_ok(game.get_bare_shot_count() == bare0 + 1, "it is tagged as a bare shot",
			"bare %d -> %d" % [bare0, game.get_bare_shot_count()])
	_ok(game.get_accuracy() < acc0, "accuracy drops when firing into the air",
			"%.1f%% -> %.1f%%" % [acc0, game.get_accuracy()])
	_ok(game.get_hit_count() == hits_before_bare, "the bare shot does not count as a hit")
	# The arithmetic must be exact: hits / shots, with no special cases.
	var expect_acc: float = 100.0 * float(game.get_hit_count()) / float(maxi(game.get_shot_count(), 1))
	_ok(absf(game.get_accuracy() - expect_acc) < 0.05, "accuracy equals hits/shots exactly",
			"%.2f%% vs %.2f%%" % [game.get_accuracy(), expect_acc])

	print("=== 6b. wrong button on a target does NOT pin it (bug) ===")
	# LMB on a gray target, and RMB on a white one: that is a miss and nothing
	# more. The target must stay fully interactive, so the correct button can
	# still resolve it afterwards.
	game.debug_suppress_spawn(true)
	game.debug_reset()
	await _frames(3)
	for pair in [[true, MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT], [false, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_LEFT]]:
		var want_gray: bool = pair[0]
		var wrong: int = pair[1]
		var right: int = pair[2]
		game.debug_clear_targets()
		game.debug_spawn_color(want_gray)
		game.debug_set_lifetime(20.0)
		await _frames(2)
		_ok(game.debug_active() == 1, "one target spawned (gray=%s)" % want_gray)
		game.debug_set_cursor(game.first_target_pos())
		await _frames(3)
		_ok(game.is_laser_locked(), "beam locks it before the wrong-button shot")
		var miss_before: int = game.get_miss_count()
		_press(wrong, true)
		await _frames(2)
		_press(wrong, false)
		await _frames(3)
		_ok(game.get_miss_count() > miss_before, "wrong button counts as a miss",
				"gray=%s" % want_gray)
		_ok(game.debug_active() == 1, "target is still on the field after the slip",
				"active=%d" % game.debug_active())
		_ok(game.is_laser_locked(), "the beam still sees it (no lock-out)",
				"gray=%s" % want_gray)
		# The payoff: the correct button must still resolve it.
		var hits_before: int = game.get_hit_count()
		if want_gray:
			_press(right, true)
			await _frames(3)
			var guard := 0
			while game.get_hold_progress() < 1.0 and guard < 3000:
				await process_frame
				guard += 1
			_press(right, false)
			await _frames(3)
		else:
			game.debug_click(game.get_cursor_pos(), true)
			await _frames(3)
		_ok(game.get_hit_count() > hits_before, "the correct button still scores it afterwards",
				"gray=%s misses=%d" % [want_gray, game.get_miss_count()])
		_ok(game.debug_active() == 0, "and it leaves the field when resolved",
				"active=%d" % game.debug_active())
	game.debug_set_lifetime(9.0)
	game.debug_suppress_spawn(false)

	print("=== 6d. dynamic difficulty: smaller targets, shorter charges ===")
	# The ramp is measured against session time, so it can be stepped directly.
	game.debug_suppress_spawn(true)
	game.debug_reset()
	await _frames(3)
	var t0: Dictionary = game.debug_tuning()
	var size0: float = t0["current_max_size"]
	var hold0: float = t0["hold_now"]
	var tol0: float = t0["hold_tol_now"]
	print("  t=0s: size=%.1f hold=%.3f tol=%.3f difficulty=%.2f" % [
			size0, hold0, tol0, t0["difficulty"]])
	_ok(t0["difficulty"] < 0.05, "difficulty starts near zero", "%.2f" % t0["difficulty"])
	_ok(absf(hold0 - t0["hold_initial"]) < 0.01, "first charges use the configured start value",
			"%.3f s" % hold0)
	# The start window is a fraction of the start hold, so it follows the setting.
	var tol_expect: float = t0["hold_initial"] * t0["hold_tol_frac"]
	_ok(absf(tol0 - tol_expect) < 0.01, "first release windows match the configured fraction",
			"%.3f s (%.0f%% of %.2f s)" % [tol0, t0["hold_tol_frac"] * 100.0, t0["hold_initial"]])

	# Step through the whole ramp and watch both values fall monotonically.
	var prev_size := size0
	var prev_hold := hold0
	var monotonic := true
	for step in 16:
		var t: float = 20.0 * float(step + 1)
		game.debug_set_session_time(t)
		await _frames(2)
		var d: Dictionary = game.debug_tuning()
		if step % 3 == 2 or step >= 14:
			print("  t=%.0fs: size=%.1f hold=%.3f tol=%.3f difficulty=%.2f" % [
					t, d["current_max_size"], d["hold_now"], d["hold_tol_now"], d["difficulty"]])
		if d["current_max_size"] > prev_size + 0.001 or d["hold_now"] > prev_hold + 0.001:
			monotonic = false
		prev_size = d["current_max_size"]
		prev_hold = d["hold_now"]
	_ok(monotonic, "size cap and charge time never grow back during the ramp")

	var tmax: Dictionary = game.debug_tuning()
	_ok(tmax["difficulty"] > 0.99, "ramp reaches full strength", "%.2f" % tmax["difficulty"])
	_ok(tmax["current_max_size"] < size0, "max target size shrank",
			"%.1f -> %.1f" % [size0, tmax["current_max_size"]])
	var target_min: float = tmax["min_size"]
	_ok(absf(tmax["current_max_size"] - target_min) < 0.5,
			"at full strength the cap reaches min_size",
			"%.1f vs min %.1f" % [tmax["current_max_size"], target_min])
	_ok(tmax["hold_now"] < hold0, "required charge time shrank",
			"%.3f -> %.3f s" % [hold0, tmax["hold_now"]])
	_ok(absf(tmax["hold_now"] - 0.5) < 0.01, "at full strength charges are at hold_min",
			"%.3f s" % tmax["hold_now"])
	_ok(tmax["hold_tol_now"] < tol0, "release window tightened with it",
			"%.3f -> %.3f s" % [tol0, tmax["hold_tol_now"]])
	_ok(tmax["hold_tol_now"] >= 0.15 - 0.001, "release window stays achievable",
			"%.3f s" % tmax["hold_tol_now"])

	# A target spawned at full difficulty must carry the hard values itself.
	game.debug_clear_targets()
	game.debug_spawn_color(true)
	await _frames(2)
	game.debug_set_cursor(game.first_target_pos())
	await _frames(3)
	_ok(game.is_laser_locked(), "beam locks the late-session gray target")
	_press(MOUSE_BUTTON_RIGHT, true)
	await _frames(2)
	var spawned_hold: float = game.debug_holding_duration_s()
	_ok(absf(spawned_hold - tmax["hold_now"]) < 0.05,
			"a late-session gray target demands the ramped charge",
			"target=%.3f s, ramp says %.3f s" % [spawned_hold, tmax["hold_now"]])
	_press(MOUSE_BUTTON_RIGHT, false)
	await _frames(2)
	# Reset puts everything back.
	game.debug_reset()
	await _frames(3)
	var restored: Dictionary = game.debug_tuning()
	_ok(absf(restored["hold_now"] - restored["hold_initial"]) < 0.01 and absf(restored["difficulty"]) < 0.05,
			"reset restores the starting difficulty",
			"hold=%.3f difficulty=%.2f" % [restored["hold_now"], restored["difficulty"]])
	game.debug_suppress_spawn(false)

	print("=== 6c. the hitbox matches the painted shape ===")
	# Sweep a beam along a target's edge: everything that overlaps the painted
	# silhouette must register, or the player sees a hit that does not count.
	game.debug_suppress_spawn(true)
	game.debug_reset()
	await _frames(3)
	var edge_hits := 0
	var edge_shots := 0
	var centre_hits := 0
	var centre_shots := 0
	for shape_try in 14:
		game.debug_clear_targets()
		game.debug_spawn_color(false)
		await _frames(2)
		if game.debug_active() == 0:
			continue
		var tp: Vector2 = game.first_target_pos()
		var tr: float = game.first_target_radius()
		if tp.length() < tr + 40.0:
			continue
		game.debug_set_cursor(tp)
		await _frames(2)
		centre_shots += 1
		if game.is_laser_locked():
			centre_hits += 1
		# Aim at the silhouette's edge (perpendicular offset of ~0.98 r): the
		# beam still touches the painted shape, so it must still lock.
		var dir := tp.normalized()
		var perp := Vector2(-dir.y, dir.x)
		game.debug_set_cursor(tp + perp * (tr * 0.98))
		await _frames(2)
		edge_shots += 1
		if game.is_laser_locked():
			edge_hits += 1
	print("  centre locks %d/%d, edge locks %d/%d" % [centre_hits, centre_shots, edge_hits, edge_shots])
	_ok(centre_shots > 0 and centre_hits == centre_shots, "beam always locks a target it points at",
			"%d/%d" % [centre_hits, centre_shots])
	_ok(edge_shots > 0 and edge_hits == edge_shots, "beam locks a target whose painted edge it crosses",
			"%d/%d" % [edge_hits, edge_shots])
	game.debug_suppress_spawn(false)

	print("=== 7. RMB hold: LMB is inert during the charge (bug 2) ===")
	# Fresh session so accuracy reflects this test, not the 400 spawns of test 4.
	game.debug_suppress_spawn(true)
	game.debug_reset()
	await _frames(3)
	game.debug_clear_targets()
	game.debug_spawn_color(true)
	await _frames(2)
	_ok(game.debug_active() == 1 and game.debug_is_gray(), "one gray target spawned")
	_ok(game.get_accuracy() >= 99.9, "session starts at 100% accuracy",
			"%.1f%%" % game.get_accuracy())
	game.debug_set_cursor(game.first_target_pos())
	await _frames(3)
	_ok(game.is_laser_locked(), "beam locks the gray target")
	var need_s: float = 0.0
	var hits_before: int = game.get_hit_count()
	var score_before: int = game.get_score()
	_press(MOUSE_BUTTON_RIGHT, true)
	await _frames(2)
	need_s = game.debug_holding_duration_s()
	_ok(need_s >= 0.5 and need_s <= 3.0, "hold duration in the 0.5-3.0 s window", "%.3f s" % need_s)
	_ok(game.get_hold_progress() >= 0.0, "hold started on RMB press",
			"progress=%.2f" % game.get_hold_progress())
	_ok(game.get_hit_count() == hits_before + 1, "on-target RMB press counts as a hit")

	# Charge part way, then hammer LMB. The charge must keep running.
	var guard := 0
	while game.get_hold_progress() < 0.35 and guard < 2000:
		await process_frame
		guard += 1
	var prog_before_lmb: float = game.get_hold_progress()
	var hits_before_lmb: int = game.get_hit_count()
	# Real LMB events through the engine, plus the direct debug path.
	_press(MOUSE_BUTTON_LEFT, true)
	_press(MOUSE_BUTTON_LEFT, false)
	game.debug_click(game.get_cursor_pos(), true)
	await _frames(4)
	_ok(game.get_hit_count() == hits_before_lmb, "LMB during RMB hold fires nothing",
			"hits stayed at %d" % hits_before_lmb)
	_ok(game.get_hold_progress() > prog_before_lmb + 0.001, "charge keeps progressing through LMB",
			"%.2f -> %.2f" % [prog_before_lmb, game.get_hold_progress()])
	_ok(game.debug_holding_duration_s() > 0.0, "the hold is still active after LMB")

	# Finish the charge and release inside the tolerance.
	guard = 0
	while game.get_hold_progress() < 1.0 and guard < 2000:
		await process_frame
		guard += 1
	_ok(game.get_hold_progress() >= 1.0, "charge completes", "after %d frames" % guard)
	await _save("v_hold_charging")
	var score_before_release: int = game.get_score()
	_press(MOUSE_BUTTON_RIGHT, false)
	await _frames(4)
	_ok(game.get_score() > score_before_release, "release in the window scores",
			"score %d -> %d" % [score_before_release, game.get_score()])
	_ok(game.get_miss_count() == 0, "a correct hold produces no miss",
			"misses=%d" % game.get_miss_count())
	_ok(game.get_accuracy() >= 99.9, "a correct hold keeps accuracy at 100%",
			"acc=%.1f%%" % game.get_accuracy())
	await _save("v_hold_done")
	game.debug_suppress_spawn(false)

	print("=== 7c. focus changes do not disturb a charge ===")
	# The polled mouse state is deliberately not used anywhere, because it does
	# not observe events pushed by a script and an unfocused window reports
	# "button up" forever. That used to cancel every charge. Focus must now be
	# irrelevant to an in-progress hold, and a charge that is never released must
	# still be bounded by the target's own timer.
	game.debug_suppress_spawn(true)
	game.debug_reset()
	await _frames(3)
	game.debug_clear_targets()
	game.debug_spawn_color(true)
	await _frames(2)
	game.debug_set_cursor(game.first_target_pos())
	await _frames(3)
	_press(MOUSE_BUTTON_RIGHT, true)
	await _frames(3)
	_ok(game.debug_holding_duration_s() > 0.0, "charge active before the focus test")
	var prog_before: float = game.get_hold_progress()
	game.debug_simulate_focus_loss(true)
	await _frames(10)
	_ok(game.debug_holding_duration_s() > 0.0, "losing focus does not cancel the charge",
			"progress %.2f -> %.2f" % [prog_before, game.get_hold_progress()])
	_ok(game.get_hold_progress() > prog_before, "the charge keeps progressing while unfocused",
			"%.2f -> %.2f" % [prog_before, game.get_hold_progress()])
	game.debug_simulate_focus_loss(false)
	await _frames(4)
	_ok(game.debug_holding_duration_s() > 0.0, "regaining focus does not cancel it either")

	# The charge is never released here. It must still resolve on its own: the
	# target keeps its lifetime, so the hold cannot outlive it.
	var resolved: bool = await _wait_until_empty(14.0)
	if not resolved:
		print("  dump: ", game.debug_target_dump())
	_ok(resolved, "an unreleased charge is bounded by the target's timer",
			"active=%d" % game.debug_active())
	_press(MOUSE_BUTTON_RIGHT, false)
	await _frames(3)
	_ok(game.debug_holding_duration_s() < 0.0, "no hold is left dangling afterwards")
	game.debug_set_lifetime(9.0)
	game.debug_suppress_spawn(false)

	print("=== 8. a hold can never pin a target forever (bug 1) ===")
	# The lifetime headroom for held targets is what used to make a stranded
	# charge permanent, so the guarantee that matters is: a target left holding
	# while nothing is charging it must still expire and stay usable.
	game.debug_suppress_spawn(true)
	game.debug_set_tuning("spawn_interval", 4.0)
	game.debug_clear_targets()
	game.debug_spawn_color(true)
	game.debug_set_lifetime(2.0)
	await _frames(2)
	_ok(game.debug_active() == 1, "exactly one target for the timeout test",
			"active=%d" % game.debug_active())
	game.debug_set_cursor(game.first_target_pos())
	await _frames(3)
	# Charge it, then strand the charge: this is the state that pinned targets.
	_press(MOUSE_BUTTON_RIGHT, true)
	await _frames(3)
	_ok(game.get_hold_progress() >= 0.0, "hold active before it is stranded",
			"progress=%.2f" % game.get_hold_progress())
	game.debug_break_hold()
	await _frames(2)
	_ok(game.debug_holding_duration_s() < 0.0, "stranded charge no longer holds the target")
	print("  dump after break: ", game.debug_target_dump())

	# Now let the lifetime run out. Waited in wall-clock time, because the frame
	# rate is capped by the config and a frame count means nothing on its own.
	var expired: bool = await _wait_until_empty(12.0)
	if not expired:
		print("  dump after waiting: ", game.debug_target_dump())
	_ok(expired, "a stranded target still expires", "active=%d" % game.debug_active())

	# The exact combination that used to pin a target: still marked as held
	# while its own lifetime runs out, with no release ever arriving.
	game.debug_clear_targets()
	game.debug_spawn_color(true)
	game.debug_set_lifetime(2.0)
	await _frames(2)
	game.debug_set_cursor(game.first_target_pos())
	await _frames(3)
	_press(MOUSE_BUTTON_RIGHT, true)
	await _frames(3)
	var survived: bool = not (await _wait_until_empty(12.0))
	if survived:
		print("  dump after held-timeout: ", game.debug_target_dump())
	_ok(not survived, "a held target whose charge never releases still times out",
			"active=%d" % game.debug_active())
	_press(MOUSE_BUTTON_RIGHT, false)
	await _frames(2)

	# And the field must come back to life once spawning resumes.
	game.debug_set_lifetime(9.0)
	game.debug_set_tuning("spawn_interval", 1.10)
	game.debug_suppress_spawn(false)
	await _idle(1.5)
	_ok(game.debug_active() >= 1, "field refills after the timeout test",
			"active=%d" % game.debug_active())
	_ok(not game.debug_any_target_unreachable(), "no unreachable target left over")

	print("=== 9. settings: every adjustable value is persisted ===")
	game.debug_open_settings()
	await _frames(3)
	var rows: PackedStringArray = game.debug_menu_rows()
	print("  menu rows: ", rows)
	for expected in ["Cursor max distance", "Targets on field", "Max target size",
			"Spawn gap", "Sensitivity (inp_mousesens)", "FPS limit", "Resolution",
			"Window mode", "Hold at start", "Hold minimum", "Tolerance at start",
			"Gray target", "White target", "Gray spawn chance"]:
		_ok(rows.has(expected), "menu offers '%s'" % expected)

	# Change several values through the menu, then confirm they were written.
	var before: Dictionary = game.debug_tuning()
	_select("Max target size")
	game.debug_menu_adjust(-1)
	game.debug_menu_adjust(-1)
	_select("Spawn gap")
	game.debug_menu_adjust(1)
	_select("Sensitivity (inp_mousesens)")
	game.debug_menu_adjust(1)
	_select("Targets on field")
	game.debug_menu_adjust(1)
	_select("Hold at start")
	game.debug_menu_adjust(1)
	_select("Hold minimum")
	game.debug_menu_adjust(-1)
	_select("Tolerance at start")
	game.debug_menu_adjust(1)
	_select("Gray spawn chance")
	game.debug_menu_adjust(1)
	_select("Gray target")
	game.debug_menu_adjust(-1)
	_select("White target")
	game.debug_menu_adjust(-1)
	await _frames(3)
	var after: Dictionary = game.debug_tuning()
	_ok(after["max_size"] != before["max_size"], "max target size changed",
			"%.0f -> %.0f" % [before["max_size"], after["max_size"]])
	_ok(after["spawn_interval"] != before["spawn_interval"], "spawn gap changed",
			"%.2f -> %.2f" % [before["spawn_interval"], after["spawn_interval"]])
	_ok(after["mouse_sens"] != before["mouse_sens"], "sensitivity changed",
			"%.0f -> %.0f" % [before["mouse_sens"], after["mouse_sens"]])
	_ok(after["target_count"] != before["target_count"], "target count changed",
			"%d -> %d" % [before["target_count"], after["target_count"]])
	_ok(after["hold_initial"] != before["hold_initial"], "start-of-run hold changed",
			"%.2f -> %.2f s" % [before["hold_initial"], after["hold_initial"]])
	_ok(after["hold_min"] != before["hold_min"], "hold floor changed",
			"%.2f -> %.2f s" % [before["hold_min"], after["hold_min"]])
	_ok(after["hold_tol_frac"] != before["hold_tol_frac"], "start tolerance changed",
			"%.2f -> %.2f" % [before["hold_tol_frac"], after["hold_tol_frac"]])
	_ok(after["gray_chance"] != before["gray_chance"], "gray spawn chance changed",
			"%.2f -> %.2f" % [before["gray_chance"], after["gray_chance"]])
	_ok(after["color_gray"] != before["color_gray"], "gray target colour changed",
			"%s -> %s" % [before["color_gray"].to_html(false), after["color_gray"].to_html(false)])
	_ok(after["color_white"] != before["color_white"], "white target colour changed",
			"%s -> %s" % [before["color_white"].to_html(false), after["color_white"].to_html(false)])
	_ok(after["hold_initial"] > after["hold_min"], "hold floor stays below the start value",
			"%.2f vs %.2f" % [after["hold_min"], after["hold_initial"]])

	var keys: PackedStringArray = game.debug_config_keys()
	print("  config keys: ", keys)
	for expected_key in ["display/max_fps", "display/window_mode", "display/window_width",
			"display/window_height", "input/mouse_sens", "input/mouse_max_distance",
			"gameplay/target_count", "gameplay/max_target_size", "gameplay/spawn_interval",
			"hold/hold_initial", "hold/hold_min", "hold/hold_tolerance_frac",
			"hold/hold_tolerance_min", "targets/gray_chance", "targets/color_gray",
			"targets/color_white"]:
		_ok(keys.has(expected_key), "config persisted %s" % expected_key)
	await _save("v_settings")

	print("=== 10. manual entry for Cursor max distance ===")
	_select("Cursor max distance")
	await _frames(2)
	var typed_before: float = game.debug_tuning()["mouse_max_distance"]
	# ENTER opens the editor, then digits, then ENTER applies.
	_key(KEY_ENTER)
	await _frames(2)
	_ok(game.debug_is_editing(), "ENTER starts numeric entry on Cursor max distance")
	for ch in ["6", "5", "0"]:
		_key(KEY_0 + ch.to_int())
		await _frames(1)
	_ok(not game.debug_edit_buffer().is_empty(), "typed digits are captured",
			"buffer='%s'" % game.debug_edit_buffer())
	_key(KEY_ENTER)
	await _frames(3)
	_ok(not game.debug_is_editing(), "ENTER commits the value")
	var typed_after: float = game.debug_tuning()["mouse_max_distance"]
	_ok(int(typed_after) == 650, "typed value applied", "%d (was %.0f)" % [int(typed_after), typed_before])
	_ok(game.debug_config_keys().has("input/mouse_max_distance"), "typed value persisted")

	# Out-of-range input must be clamped, not accepted raw.
	_key(KEY_ENTER)
	await _frames(2)
	for ch in ["9", "9", "9", "9", "9"]:
		_key(KEY_0 + ch.to_int())
		await _frames(1)
	_key(KEY_ENTER)
	await _frames(3)
	_ok(game.debug_tuning()["mouse_max_distance"] <= 2000.0, "out-of-range entry is clamped",
			"%.0f" % game.debug_tuning()["mouse_max_distance"])
	# Put it back so the rest of the run is unaffected.
	_key(KEY_ENTER)
	await _frames(2)
	for ch in ["4", "0", "0"]:
		_key(KEY_0 + ch.to_int())
		await _frames(1)
	_key(KEY_ENTER)
	await _frames(3)
	_ok(int(game.debug_tuning()["mouse_max_distance"]) == 400, "value restored",
			"%.0f" % game.debug_tuning()["mouse_max_distance"])

	print("=== 10b. manual entry for the new settings ===")
	# Hold time and tolerance are typed in hundredths / percent.
	_select("Hold at start")
	await _frames(2)
	_key(KEY_ENTER)
	await _frames(2)
	_ok(game.debug_is_editing(), "ENTER starts entry on Hold at start")
	for ch in ["2", "5", "0"]:
		_key(KEY_0 + ch.to_int())
		await _frames(1)
	_key(KEY_ENTER)
	await _frames(3)
	_ok(absf(game.debug_tuning()["hold_initial"] - 2.5) < 0.01, "typed hold applied",
			"%.2f s" % game.debug_tuning()["hold_initial"])

	_select("Tolerance at start")
	await _frames(2)
	_key(KEY_ENTER)
	await _frames(2)
	for ch in ["4", "0"]:
		_key(KEY_0 + ch.to_int())
		await _frames(1)
	_key(KEY_ENTER)
	await _frames(3)
	_ok(absf(game.debug_tuning()["hold_tol_frac"] - 0.40) < 0.01, "typed tolerance applied",
			"%.2f of the hold" % game.debug_tuning()["hold_tol_frac"])
	# 40% of a 2.5 s hold is 1.0 s, so the live window must follow.
	game.debug_set_session_time(0.0)
	await _frames(2)
	_ok(absf(game.debug_tuning()["hold_tol_now"] - 1.0) < 0.02,
			"the live release window follows the settings",
			"%.2f s" % game.debug_tuning()["hold_tol_now"])

	# Gray chance as a percentage.
	_select("Gray spawn chance")
	await _frames(2)
	_key(KEY_ENTER)
	await _frames(2)
	for ch in ["3", "0"]:
		_key(KEY_0 + ch.to_int())
		await _frames(1)
	_key(KEY_ENTER)
	await _frames(3)
	_ok(absf(game.debug_tuning()["gray_chance"] - 0.30) < 0.01, "typed gray chance applied",
			"%.0f%%" % (game.debug_tuning()["gray_chance"] * 100.0))

	print("=== 10c. colour hex entry ===")
	_select("Gray target")
	await _frames(2)
	_key(KEY_ENTER)
	await _frames(2)
	_ok(game.debug_is_editing(), "ENTER starts hex entry on Gray target")
	# "#3366CC" typed as digits and hex letters.
	for k in [KEY_NUMBERSIGN, KEY_3, KEY_3, KEY_6, KEY_6, KEY_C, KEY_C]:
		_key(k)
		await _frames(1)
	print("  typed colour buffer: '", game.debug_edit_buffer(), "'")
	_key(KEY_ENTER)
	await _frames(3)
	var picked: Color = game.debug_tuning()["color_gray"]
	print("  resulting gray colour: ", picked.to_html(false))
	_ok(absf(picked.r - 0x33 / 255.0) < 0.01 and absf(picked.g - 0x66 / 255.0) < 0.01 and absf(picked.b - 0xCC / 255.0) < 0.01,
			"hex colour applied to gray targets", "#%s" % picked.to_html(false))
	_ok(game.debug_config_keys().has("targets/color_gray"), "colour persisted")

	# Nonsense input must be discarded rather than blanking the colour.
	_key(KEY_ENTER)
	await _frames(2)
	for k in [KEY_Z, KEY_Z, KEY_Z]:
		_key(k)
		await _frames(1)
	_key(KEY_ENTER)
	await _frames(3)
	var kept: Color = game.debug_tuning()["color_gray"]
	_ok(absf(kept.b - 0xCC / 255.0) < 0.01, "unparseable colour is discarded, not applied",
			"#%s" % kept.to_html(false))

	print("=== 10d. spawn chance actually drives the mix ===")
	# With the chance pinned to 100% and then 0%, every spawned target must follow.
	for probe in [[1.0, true], [0.0, false]]:
		var chance: float = probe[0]
		var expect_gray: bool = probe[1]
		game.debug_set_tuning("gray_chance", chance)
		var wrong := 0
		var seen := 0
		for i in 40:
			game.debug_clear_targets()
			game.debug_spawn_target()
			await process_frame
			if game.debug_active() == 0:
				continue
			seen += 1
			if game.debug_is_gray() != expect_gray:
				wrong += 1
		_ok(seen > 0 and wrong == 0, "gray chance %.0f%% produces only %s targets" % [
				chance * 100.0, "gray" if expect_gray else "white"],
				"%d checked, %d wrong" % [seen, wrong])
	game.debug_set_tuning("gray_chance", 0.45)

	print("=== 10e. custom colours reach the field ===")
	# Paint the two kinds in unmistakable colours and confirm they appear in the
	# rendered frame, which proves the tuning is actually used by the renderer.
	game.debug_set_tuning("hold_initial", 3.0)
	game.debug_suppress_spawn(true)
	game.debug_reset()
	await _frames(3)
	var painted: Color = Color(1.0, 0.0, 0.0)
	game.debug_set_color("gray", painted)
	game.debug_set_color("white", Color(0.0, 0.4, 1.0))
	game.debug_clear_targets()
	game.debug_spawn_color(true)
	game.debug_spawn_color(false)
	await _frames(35)
	var shot: Image = null
	if DisplayServer.get_name() != "headless":
		shot = root.get_texture().get_image()
	if shot != null:
		var w: int = shot.get_width()
		var h: int = shot.get_height()
		# The playfield is the middle of the frame; sample a coarse grid and look
		# for both painted colours.
		var found_red := false
		var found_blue := false
		for gx in 48:
			for gy in 32:
				var c: Color = shot.get_pixel(int((gx + 0.5) * w / 48.0), int((gy + 0.5) * h / 32.0))
				if c.r > 0.6 and c.g < 0.35 and c.b < 0.35:
					found_red = true
				if c.b > 0.6 and c.r < 0.35:
					found_blue = true
		_ok(found_red, "the custom gray colour is rendered on the field")
		_ok(found_blue, "the custom white colour is rendered on the field")
	else:
		_ok(true, "colour render check skipped (headless)")
	await _save("v_custom_colors")
	# Put the default palette back.
	game.debug_set_color("gray", Color(0.55, 0.55, 0.58))
	game.debug_set_color("white", Color(0.97, 0.97, 0.98))
	game.debug_suppress_spawn(false)
	game.debug_close_menu()
	await _frames(2)

	print("=== 11. crowded field and reset ===")
	game.debug_clear_targets()
	game.debug_spawn_color(false)
	game.debug_spawn_color(false)
	await _frames(2)
	_ok(game.debug_active() >= 2, "multiple targets alive", "active=%d" % game.debug_active())
	game.debug_set_cursor(game.first_target_pos())
	await _frames(2)
	_ok(game.debug_laser_has_target(), "beam resolves a target on a crowded field")
	game.debug_reset()
	await _frames(3)
	_ok(game.get_score() == 0 and game.get_miss_count() == 0, "reset clears the session")
	_ok(game.get_target_count() == game.debug_tuning()["target_count"],
			"reset respawns the configured field", "targets=%d" % game.get_target_count())
	_ok(not game.debug_any_target_unreachable(), "respawned targets are all reachable")

	print("=== 12. live motion ===")
	game.debug_freeze_motion(false)
	game.debug_reset()
	await _idle(1.0)
	game.debug_set_cursor(Vector2(0.8, -0.6).normalized() * 380.0)
	await _idle(0.4)
	await _save("v_motion")
	_ok(game.get_target_count() >= 1, "field stays populated with motion live")

	print("=== 13. screens ===")
	game.debug_open_results()
	await _frames(3)
	await _save("v_results")
	game.debug_open_settings()
	await _frames(3)
	await _save("v_settings2")
	game.debug_close_menu()

	print("=== %d checks, %d failures ===" % [checks, fails])
	print("DONE")
	_finish()
