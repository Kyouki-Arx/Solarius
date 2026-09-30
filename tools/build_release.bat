@echo off
rem Builds the release GDExtension library.
rem Usage: tools\build_release.bat [platform]
setlocal
set "PLATFORM=%~1"
if "%PLATFORM%"=="" set "PLATFORM=windows"
pushd "%~dp0.."
scons platform=%PLATFORM% target=template_release -j8
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%
