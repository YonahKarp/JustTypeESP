@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if not "%errorlevel%"=="0" exit /b %errorlevel%
python tools/content/build_packages.py
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /c /TC /I C:/Users/karpy/esp/esp-idf-v5.5.6/components/json/cJSON C:/Users/karpy/esp/esp-idf-v5.5.6/components/json/cJSON/cJSON.c /Fo:build/content-cjson.obj
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /std:c++17 /utf-8 /EHsc /DLEAF_CONTENT_RUNTIME=1 /DLEAF_CONTENT_ROOT=\"build/content-test-data\" /I tools/content_tests /I main/justtype/src /I main /I C:/Users/karpy/esp/esp-idf-v5.5.6/components/json/cJSON main/justtype/src/content.cpp main/justtype/src/spelling/spellcheck_core.cpp tools/content_tests/main.cpp build/content-cjson.obj /Fo:build/ /Fe:build/content-tests.exe
if not "%errorlevel%"=="0" exit /b %errorlevel%
build\content-tests.exe
if not "%errorlevel%"=="0" exit /b %errorlevel%
build\content-tests.exe --restart
