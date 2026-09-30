# Solarius

A 2D aim trainer built as a Godot 4.3 GDExtension. All gameplay logic is C++
(`src/`); Godot only supplies the window, input events and a canvas to draw on.

You aim a **laser** that leaves the centre of the screen, passes through your
cursor and keeps going. A click resolves against the nearest target the beam
actually crosses — not against whatever the cursor happens to sit on.

## Gameplay

- **Laser aiming.** The beam runs from the player point through the cursor and
  past it. Hit detection is a capsule-versus-silhouette test, so a beam whose
  glow visibly crosses a target always registers. The target nearest the centre
  wins.
- **Two target kinds.** White targets are scored with `LMB`; gray targets must be
  held with `RMB` for a required charge time and released inside a tolerance
  window. The wrong button is a miss and nothing more — the target stays
  playable, so one slip is not punished twice.
- **Anywhere on screen.** Targets spawn uniformly across the whole visible
  playfield, well outside the cursor's movement circle, because the laser only
  has to *point* at them.
- **Dynamic difficulty.** One session-time ramp shrinks the maximum target size
  and shortens the required charge from its starting value down to a configured
  floor, with the release window tightening alongside it.
- **Accuracy** is hits over *every* shot fired, including shots into open space.

Everything above is configurable in-game via `TAB`: target count and size, spawn
gap, hold timing, tolerance, target colours (hex or `A`/`D`), the gray/white mix,
mouse sensitivity and `cl_mouse_max_distance`. Settings persist to
`user://aimtrainer.cfg`.

### Controls

| Key | Action |
| --- | --- |
| Mouse | Aim the laser |
| `LMB` | Shoot — scores white targets |
| `RMB` (hold) | Charge — scores gray targets on release |
| `R` | Reset the session |
| `ESC` | Results screen |
| `TAB` | Settings |
| `W/S` `A/D`, arrows, `Enter` | Menu navigation / apply / type a value |

## Building

### Requirements

- [Godot 4.3](https://godotengine.org/download/archive/4.3-stable/) (the editor
  binary is used to run the project from source)
- [SCons](https://scons.org/) and Python 3
- A C++17 toolchain: MinGW-w64 on Windows, `g++` on Linux, Xcode CLT on macOS

### Steps

```bash
git clone --recurse-submodules https://github.com/Kyouki-Arx/Solarius.git
cd Solarius

# Release build (what the shipped binaries use)
scons platform=windows target=template_release -j8   # or platform=linux / macos
# Debug build (what an editor run loads)
scons platform=windows target=template_debug -j8
```

`godot-cpp` is pinned as a submodule at the `godot-4.3-stable` tag. If you cloned
without `--recurse-submodules`, run `git submodule update --init`. To build
against a checkout elsewhere, pass `godot_cpp_path=/path/to/godot-cpp`.

### Running from source

Point an editor build of Godot at the project:

```bash
godot --path .          # or: tools\run.bat  on Windows
```

## Why the release DLL needs its own project folder

The editor build of Godot reports the features `debug` and `editor`, never
`template_release`, and GDExtension library selection is driven by those feature
strings. A project that lists the release DLL under `windows.release.x86_64`
therefore cannot load it from an editor binary — the optimised library is
unreachable and the editor silently uses the debug one.

The shipped `Solarius.exe` does not have this problem: an exported build reports
`template_release` and loads `bin/libaimtrainer...template_release...dll`
directly. Only running from source needs the debug build.

The window title reading `(DEBUG)` is a property of the editor binary's title
string, not of the library that got loaded. To see which one is live:

```powershell
Get-Process godot | ForEach-Object { $_.Modules } |
  Where-Object { $_.FileName -match 'aimtrainer' } | Select-Object FileName
```

## Downloads

Prebuilt binaries are attached to the Releases page. The Windows 64-bit package
is a self-contained folder — extract it anywhere and run `Solarius.exe`.

Godot 4 has **no 32-bit desktop builds**: upstream dropped x86 and 32-bit ARM
support in 4.0, and no export template for them exists. 32-bit Windows is
therefore not available in any form, for this or any other Godot 4 project.

Cross-compiling desktop builds is also not supported by Godot. The source here is
portable; build it on the target OS with the commands above.

## Layout

| Path | Contents |
| --- | --- |
| `src/aim_trainer.{h,cpp}` | Game loop, input, laser geometry, spawning, hold state machine, effects |
| `src/aim_trainer_draw.cpp` | Background, targets, beam, particles, HUD, results and settings screens |
| `src/aim_trainer_menu.cpp` | Settings menu, persistence, config parsing |
| `src/session_stats.{h,cpp}` | Score, accuracy, hold deviations, shot intervals, miss records |
| `src/target.{h,cpp}` | Target struct, movement easing, shape tests |
| `src/ddnet_input.{h,cpp}` | DDNet-compatible sensitivity and cursor clamping |
| `src/register_types.cpp` | GDExtension entry point |
| `tools/` | Launcher scripts (the Godot binary itself is not committed) |
| `visual_check.gd` | End-to-end test harness, see below |

## Tests

`visual_check.gd` drives the real input path — it pushes synthetic mouse events
through the engine rather than calling internals — and asserts 149 checks, then
writes PNGs of each state to `user://`.

```bash
godot --path . --script res://visual_check.gd
```

It stashes your `aimtrainer.cfg` on start and restores it on exit, so running the
suite never clobbers your settings.

Notes on the harness:

- It needs a real window: the playfield is sized from the viewport, so under
  `--headless` the spawn-area assertions cannot pass.
- `Input.is_mouse_button_pressed()` does **not** observe events injected with
  `push_input`, and an unfocused window reports "button up" forever. The game
  therefore never polls mouse state; the event stream is the only source of
  truth, which is what lets a charge survive focus loss.

## DDNet compatibility

The cursor handling in `src/ddnet_input.*` re-implements DDNet's model
(`inp_mousesens`, `cl_mouse_max_distance`, `ClampMousePos`) against
[DDNet](https://github.com/ddnet/ddnet). The original project is neither
modified nor vendored; the formulas are reimplemented in Godot units.

## License

No license file is present yet. Until one is added the code is
all-rights-reserved by default — add a license before accepting contributions.
