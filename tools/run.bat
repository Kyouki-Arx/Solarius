@echo off
rem Runs the trainer from this checkout using an editor build of Godot.
rem Usage: tools\run.bat [extra godot arguments]
setlocal
"%~dp0godot.exe" --path "%~dp0.." %*
exit /b %ERRORLEVEL%
