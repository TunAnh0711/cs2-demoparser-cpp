@echo off
rem Builds cs2demo_esp with MSVC without relying on vcvars64.bat (which needs
rem reg.exe).  Adjust the toolset paths if your Visual Studio install differs.
setlocal

set "VCTOOLS=C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231"
set "SDK=C:/Program Files (x86)/Windows Kits/10"
set "SDKVER=10.0.26100.0"

if not exist "%VCTOOLS%" (
  echo [build] MSVC toolset not found at %VCTOOLS%
  exit /b 1
)

set "PATH=%VCTOOLS%\bin\Hostx64\x64;%PATH%"
set "INCLUDE=%VCTOOLS%\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\shared;%SDK%\Include\%SDKVER%\winrt"
set "LIB=%VCTOOLS%\lib\x64;%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64"

cd /d "%~dp0"

if not exist out mkdir out

cl /nologo /EHsc /std:c++20 /W3 /permissive- /utf-8 /O2 /MP ^
   /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DWIN32_LEAN_AND_MEAN ^
   src\main.cpp src\demo_parser.cpp src\serializer.cpp src\fieldpath.cpp ^
   src\valve_messages.cpp src\snappy.cpp src\esp.cpp ^
   /Fe:out\cs2demo_esp.exe /Fo:out\ ^
   /link /SUBSYSTEM:CONSOLE gdi32.lib user32.lib shell32.lib

endlocal
