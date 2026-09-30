# Building Solarius

Everything here builds the same thing: `bin/libaimtrainer<platform><target>.<ext>`,
the GDExtension library holding all gameplay code. Godot then loads it alongside
`project.godot`.

## 1. Get the sources

```bash
git clone --recurse-submodules https://github.com/Kyouki-Arx/Solarius.git
cd Solarius
```

`godot-cpp` is a submodule pinned to the `godot-4.3-stable` tag. If you forgot
`--recurse-submodules`:

```bash
git submodule update --init
```

## 2. Install the toolchain

| Platform | Compiler | Package manager |
| --- | --- | --- |
| Windows | MinGW-w64 `g++` (MSYS2 `mingw-w64-x86_64-gcc`) | `pacman -S mingw-w64-x86_64-gcc` |
| Linux | `g++` 9+ | `sudo apt install build-essential` |
| macOS | Xcode command line tools | `xcode-select --install` |

SCons comes from pip on every platform:

```bash
pip install scons
```

## 3. Build the library

```bash
# Windows
scons platform=windows target=template_release -j8
scons platform=windows target=template_debug   -j8

# Linux
scons platform=linux target=template_release -j8
scons platform=linux target=template_debug   -j8

# macOS
scons platform=macos target=template_release -j8
scons platform=macos target=template_debug   -j8
```

Both targets matter, and for different reasons:

- **`template_debug`** is what an editor build of Godot loads when you run the
  project from source (`godot --path .`).
- **`template_release`** is what an exported game loads. It is the optimised
  build that ships.

To build against a `godot-cpp` checkout outside this repository:

```bash
scons platform=windows target=template_release godot_cpp_path=/path/to/godot-cpp -j8
```

## 4. Run or export

Run from source (needs an editor build of Godot 4.3):

```bash
godot --path .
```

Export a standalone build:

```bash
# One-time: install the matching export templates
#   https://github.com/godotengine/godot/releases/download/4.3-stable/Godot_v4.3-stable_export_templates.tpz
# Unpack into:
#   Windows: %APPDATA%\Godot\export_templates\4.3.stable\
#   Linux:   ~/.local/share/godot/export_templates/4.3.stable/
#   macOS:   ~/Library/Application Support/Godot/export_templates/4.3.stable/

godot --headless --path . --export-release "Windows Desktop" build/Solarius.exe
godot --headless --path . --export-release "Linux/X11"      build/Solarius.x86_64
godot --headless --path . --export-release "macOS"          build/Solarius.zip
```

Export presets are **not** committed (`export_presets.cfg` is git-ignored),
because they bake in absolute paths and machine-specific settings. Create them
once in the editor via `Project → Export`, or write your own file — the presets
only need to include `bin/*` as additional files.

## Platform notes

### 32-bit desktop is not possible

Godot 4 dropped support for x86 and 32-bit ARM at the 4.0 release. There is no
`windows_x86_32` export template for any Godot 4 version, so a 32-bit build of
this project cannot be produced by any means. This is an upstream decision, not
a limitation of this codebase.

### Cross-compiling is not supported

Godot's export templates are not cross-compilers. Exporting a Linux binary
requires running Godot on Linux; the same applies to macOS. MinGW on Windows can
technically produce Linux binaries, but Godot has no supported path for it, so
build on the target OS instead.

### macOS specifics

- Godot can produce macOS binaries only from macOS, and they must be code-signed
  and notarised to run on other machines without a Gatekeeper override.
- An unsigned build can be opened locally with
  `xattr -dr com.apple.quarantine Solarius.app`.
- Building a universal (Intel + Apple Silicon) binary requires both
  architectures' export templates and is best driven from the editor's export
  dialog.

### Windows specifics

- The MinGW toolchain used here must match the templates' expectations; the
  official `mingw-w64-x86_64-gcc` from MSYS2 works.
- The shipped package includes the Godot runtime, so no separate Godot install
  is needed on the target machine.
