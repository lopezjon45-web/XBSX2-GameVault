@echo off
rem Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
rem Builds the Xbox (UWP) package of this XBSX2 tree with the Visual Studio 2022 Build Tools.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
msbuild PCSX2_qt.sln /m /v:m /nologo /p:PlatformToolset=v143 /p:Configuration="Release" /p:Platform=x64 /t:restore /p:RestorePackagesConfig=true || exit /b 1
msbuild PCSX2_qt.sln /m /v:m /nologo /p:PlatformToolset=v143 /p:Configuration="Release" /p:Platform=x64 /t:pcsx2-uwp %*
