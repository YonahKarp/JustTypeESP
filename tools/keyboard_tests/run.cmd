@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
python tools/spellcheck/build_dictionary.py
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /I tools/keyboard_tests /I main main/keyboard_config.cpp main/justtype/src/spelling/spellcheck_core.cpp main/justtype/src/spelling/viewport_checker.cpp tools/keyboard_tests/navigation_tests.cpp tools/keyboard_tests/viewport_tests.cpp tools/keyboard_tests/backup_policy_tests.cpp main/justtype/src/text_navigation.cpp main/justtype/src/search.cpp tools/keyboard_tests/main.cpp /Fo:tools/keyboard_tests/ /Fe:tools/keyboard_tests/keyboard_tests.exe
if errorlevel 1 exit /b 1
tools\keyboard_tests\keyboard_tests.exe
