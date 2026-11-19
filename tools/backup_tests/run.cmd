@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /I tools/backup_tests/stubs /I main tools/backup_tests/main.cpp /Fo:tools/backup_tests/ /Fe:tools/backup_tests/storage_tests.exe
if errorlevel 1 exit /b 1
tools\backup_tests\storage_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /I tools/backup_tests/stubs /I main tools/backup_tests/http_tests.cpp /Fo:tools/backup_tests/ /Fe:tools/backup_tests/http_tests.exe
if errorlevel 1 exit /b 1
tools\backup_tests\http_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /I tools/backup_tests/stubs /I main tools/backup_tests/dirty_tests.cpp /Fo:tools/backup_tests/ /Fe:tools/backup_tests/dirty_tests.exe
if errorlevel 1 exit /b 1
tools\backup_tests\dirty_tests.exe
