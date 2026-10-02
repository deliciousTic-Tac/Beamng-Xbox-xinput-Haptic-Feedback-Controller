@echo off
setlocal
rem test = unsigned candidate; release = signing mandatory. Never overwrite the old DLL.
set "BUILD_MODE=%~1"
if "%BUILD_MODE%"=="" set "BUILD_MODE=test"
if /I not "%BUILD_MODE%"=="test" if /I not "%BUILD_MODE%"=="release" (
  echo Usage: build.cmd [test^|release]
  exit /b 1
)
if /I "%BUILD_MODE%"=="release" if "%CODE_SIGN_CERT_THUMBPRINT%"=="" (
  echo ERROR: release requires CODE_SIGN_CERT_THUMBPRINT. Use test for an unsigned candidate.
  exit /b 1
)
pushd "%~dp0"
if errorlevel 1 exit /b 1
rem A portable/custom installation can set HAPTICS_VS_ROOT explicitly.
set "VSROOT=%HAPTICS_VS_ROOT%"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VSROOT if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
)
if not defined VSROOT (
  for %%I in ("%ProgramFiles%\Microsoft Visual Studio\2022\Community" "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools" "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools") do (
    if exist "%%~fI\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=%%~fI"
  )
)
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" (
  echo ERROR: MSVC x64 and Windows SDK are missing. Install C++ Build Tools or set HAPTICS_VS_ROOT.
  goto :failed
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :failed
set "OUT=out\%BUILD_MODE%"
if not exist "%OUT%" mkdir "%OUT%"
if errorlevel 1 goto :failed
rem Capture inputs before compilation; packaging rejects changed sources or stale DLLs.
powershell.exe -NoProfile -File tools\artifacts.ps1 -Action Begin -Mode "%BUILD_MODE%"
if errorlevel 1 goto :failed
rc /nologo /fo "%OUT%\version.res" version.rc
if errorlevel 1 goto :failed
cl /nologo /std:c++17 /EHsc /MT /W4 /O2 /GL /guard:cf /LD xinput_proxy.cpp "%OUT%\version.res" /Fo"%OUT%\xinput_proxy.obj" /Fe:"%OUT%\XInput1_4.dll" /link /LTCG /guard:cf /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /Brepro /INCREMENTAL:NO /DEF:xinput_proxy.def /IMPLIB:"%OUT%\XInput1_4.lib" ws2_32.lib runtimeobject.lib windowsapp.lib
if errorlevel 1 goto :failed
cl /nologo /std:c++17 /EHsc /MT /W4 /O2 /guard:cf tests\native_tests.cpp /Fo"%OUT%\native_tests.obj" /Fe:"%OUT%\native_tests.exe" /link /guard:cf ws2_32.lib runtimeobject.lib windowsapp.lib
if errorlevel 1 goto :failed
"%OUT%\native_tests.exe" > "%OUT%\native-tests.log" 2>&1
if errorlevel 1 goto :failed
if not defined HAPTICS_PYTHON set "HAPTICS_PYTHON=python"
"%HAPTICS_PYTHON%" tests\test_lua.py > "%OUT%\lua-tests.log" 2>&1
if errorlevel 1 goto :failed
"%HAPTICS_PYTHON%" tests\test_artifacts.py > "%OUT%\artifact-tests.log" 2>&1
if errorlevel 1 goto :failed
rem load_test is opt-in: it calls real WinRT and may address a connected controller.
cl /nologo /std:c++17 /EHsc /MT /W4 /O2 load_test.cpp /Fo"%OUT%\load_test.obj" /Fe:"%OUT%\load_test.exe" runtimeobject.lib
if errorlevel 1 goto :failed
if /I "%BUILD_MODE%"=="release" (
  where signtool >nul 2>nul
  if errorlevel 1 goto :failed
  if not defined CODE_SIGN_TIMESTAMP_URL set "CODE_SIGN_TIMESTAMP_URL=http://timestamp.digicert.com"
  call :sign
  if errorlevel 1 goto :failed
)
powershell.exe -NoProfile -File tools\artifacts.ps1 -Action Finish -Mode "%BUILD_MODE%"
if errorlevel 1 goto :failed
echo SUCCESS: %CD%\%OUT%\XInput1_4.dll
echo Next: powershell -NoProfile -File tools\artifacts.ps1 -Action Package -Mode %BUILD_MODE%
popd
exit /b 0

:sign
signtool sign /fd SHA256 /sha1 "%CODE_SIGN_CERT_THUMBPRINT%" /tr "%CODE_SIGN_TIMESTAMP_URL%" /td SHA256 "%OUT%\XInput1_4.dll"
if errorlevel 1 exit /b 1
signtool verify /pa "%OUT%\XInput1_4.dll"
exit /b %errorlevel%

:failed
echo FAILED: no validated package was produced. Do not distribute an older output DLL.
popd
exit /b 1
