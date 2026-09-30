#!/usr/bin/env python
"""Build script for the Aim Trainer GDExtension.

godot_cpp_path resolution order:
  1. the godot_cpp_path=... command line argument
  2. the GODOT_CPP_PATH environment variable
  3. ./godot-cpp  (the pinned submodule; run `git submodule update --init`)

Example:
    scons platform=windows target=template_release -j8
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

if env["platform"] == "windows":
    env.Append(LINKFLAGS=["-static-libgcc", "-static-libstdc++"])
    env.Append(CPPDEFINES=["NOMINMAX", "WIN32_LEAN_AND_MEAN"])

library = env.SharedLibrary(
    "bin/libaimtrainer{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
    source=sources,
)
Default(library)
