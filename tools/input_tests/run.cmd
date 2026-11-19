@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /I tools/input_tests /I main main/keyboard_input.cpp main/keyboard_config.cpp tools/input_tests/main.cpp /Fo:tools/input_tests/ /Fe:tools/input_tests/input_tests.exe
if errorlevel 1 exit /b 1
tools\input_tests\input_tests.exe
