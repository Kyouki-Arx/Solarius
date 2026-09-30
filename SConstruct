#!/usr/bin/env python
"""Build script for the Solarius GDExtension.

godot_cpp_path resolution order:
  1. the godot_cpp_path=... command line argument
  2. the GODOT_CPP_PATH environment variable
  3. ./godot-cpp  (the pinned submodule; run `git submodule update --init`)

Targets follow Godot's own template naming: <platform>.<target>.<arch>. The
architecture defaults to the host's, and is what makes one project directory
able to hold several builds side by side (Godot picks the matching library at
load time from the `arch` row of the .gdextension manifest).

Examples:
    scons platform=windows target=template_release                 # host arch
    scons platform=windows target=template_release arch=x86_32     # 32-bit
    scons platform=linux   target=template_release                 # needs a
                                                                   # Linux toolchain
"""
import os

godot_cpp_path = ARGUMENTS.get(  # noqa: F821  (SCons injected)
    "godot_cpp_path",
    os.environ.get("GODOT_CPP_PATH", "godot-cpp"),
)

env = SConscript(os.path.join(godot_cpp_path, "SConstruct"))

env.Append(CPPPATH=["src/"])
env.Append(CPPDEFINES=["NDEBUG"])
# Source files are UTF-8. Without this MinGW passes string literals through as
# single-byte codepoints and every non-ASCII character renders as mojibake.
env.Append(CCFLAGS=["-finput-charset=UTF-8", "-fexec-charset=UTF-8"])
sources = Glob("src/*.cpp")

arch = ARGUMENTS.get("arch", env["arch"])

if env["platform"] == "windows":
    env.Append(LINKFLAGS=["-static-libgcc", "-static-libstdc++"])
    env.Append(CPPDEFINES=["NOMINMAX", "WIN32_LEAN_AND_MEAN"])
    # A 32-bit target needs `-m32` on top of the generic build flags; godot-cpp
    # already applies it to its own objects, but the flags are per-project.
    if arch == "x86_32":
        env.Append(CCFLAGS=["-m32"])
        env.Append(LINKFLAGS=["-m32"])
    elif arch == "arm64":
        env.Append(CCFLAGS=["-marm"])

library = env.SharedLibrary(
    "bin/libaimtrainer.{}.{}.{}{}".format(env["platform"], env["target"], arch, env["SHLIBSUFFIX"]),
    source=sources,
)
Default(library)
