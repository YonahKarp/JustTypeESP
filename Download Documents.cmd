@echo off
setlocal
set "JUSTTYPE_PY=%USERPROFILE%\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
if exist "%JUSTTYPE_PY%" (
    "%JUSTTYPE_PY%" "%~dp0tools\download_documents.py"
) else (
    py -3 "%~dp0tools\download_documents.py"
)
if errorlevel 1 pause
