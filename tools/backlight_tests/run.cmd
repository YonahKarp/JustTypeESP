@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /I tools/backlight_tests /I tools/input_tests /I main main/backlight.cpp tools/backlight_tests/main.cpp /Fo:tools/backlight_tests/ /Fe:tools/backlight_tests/backlight_tests.exe
if errorlevel 1 exit /b 1
tools\backlight_tests\backlight_tests.exe
