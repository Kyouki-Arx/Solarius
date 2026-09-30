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
# Windows (use_mingw=yes is required when MSVC is installed, see Platform notes)
scons platform=windows target=template_release use_mingw=yes -j8
scons platform=windows target=template_debug   use_mingw=yes -j8

# 32-bit (needs an i686-capable toolchain, see Platform notes)
scons platform=windows target=template_release arch=x86_32 use_mingw=yes -j8
scons platform=linux   target=template_release arch=x86_32 -j8

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

godot --headless --path . --export-release "Windows Desktop"        build/Solarius.exe
godot --headless --path . --export-release "Windows Desktop 32-bit" build/Solarius-x86_32.exe
godot --headless --path . --export-release "Linux/X11"             build/Solarius.x86_64
godot --headless --path . --export-release "macOS"                 build/Solarius.zip
```

A working `export_presets.cfg` is committed, so these run as-is on a fresh
clone. It is still listed in `.gitignore` in case you prefer to keep
machine-specific presets untracked — if you edit it, use
`git add -f export_presets.cfg` so the release CI keeps working. The presets
only need `bin/*` included as additional files.

## Platform notes

### 32-bit desktop is possible

Godot 4 still ships 32-bit desktop export templates. `4.3.stable` includes
`windows_release_x86_32.exe`, `windows_debug_x86_32.exe`,
`linux_release.x86_32` and `linux_debug.x86_32`, so 32-bit Windows and Linux
builds of this project can be produced. (An earlier revision of this document
claimed upstream had dropped 32-bit in 4.0; that was wrong.)

What is gone since Godot 3 is 32-bit **ARM** (`arm32`) Windows, which is why
`arm64` entries here have no 32-bit counterpart.

Two practical notes for 32-bit:

- The 32-bit library must be built with a 32-bit-capable toolchain. A
  x86_64-only MinGW install can *compile* 32-bit objects but cannot *link*
  them, because the 32-bit CRT import libraries are missing. MSYS2's
  `mingw-w64-i686-gcc` (or any standalone i686 MinGW build) is required.
- `arch=x86_32` in the scons command selects it; the output lands in
  `bin/libaimtrainer.<platform>.template_<target>.x86_32.<ext>`.

### Cross-compiling

Godot's export templates are prebuilt binaries for one OS, not cross-compilers,
so an exported *game* must be produced on its target OS. The **GDExtension
library**, however, is ordinary C++ and cross-compiles freely — the Linux
`.so` and macOS dylib in `bin/` can be built from any host with a suitable
cross-toolchain. Building the library everywhere and exporting per-OS is the
workflow `.github/workflows/build.yml` automates.

### macOS specifics

- Godot 4.3 ships a **single** macOS export template (`macos.zip`), which is
  universal. There is no `godot_macos_release.x86_64`, so the export preset
  must use `binary_format/architecture="universal"` and the dylib must be built
  with `arch=universal` to match. Exporting for x86_64 alone fails with
  "Requested template binary ... not found".
- Universal and arm64 exports additionally require
  `textures/vram_compression/import_etc2_astc=true` in `project.godot`;
  without it the export aborts before writing anything.
- Godot can produce macOS binaries only from macOS, and they must be code-signed
  and notarised to run on other machines without a Gatekeeper override.
- An unsigned build can be opened locally with
  `xattr -dr com.apple.quarantine Solarius.app`.
- A bundle identifier (`application/bundle_identifier`) is required or the
  export refuses to run.

### Windows specifics

- godot-cpp picks **MSVC** over MinGW whenever a Visual Studio compiler is
  present, and MSVC builds with `/WX` (warnings are errors). On a machine that
  has both, a plain `scons platform=windows` therefore fails at link time on
  the MinGW-only flags this project passes. Pass `use_mingw=yes` to force
  MinGW, which is the toolchain the export templates expect:

  ```bash
  scons platform=windows target=template_release use_mingw=yes -j8
  ```

- The MinGW toolchain used here must match the templates' expectations; the
  official `mingw-w64-x86_64-gcc` from MSYS2 works.
- The shipped package includes the Godot runtime, so no separate Godot install
  is needed on the target machine.
