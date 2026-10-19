# Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
# Turns the bundle build_gv.bat makes (signed with XBSX2's temporary key, publisher CN=SternXD) into
# "XBSX2 for Game Vault" signed with Game Vault's key (publisher CN=GamerDad), so it updates the installed app.
#   resign_gv.ps1 <version>   ->  dist\XBSX2-for-GameVault_<version>_SSE4.msixbundle
param([string]$Ver)
$ErrorActionPreference = "Stop"
$sdk = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64"
$root = $PSScriptRoot
$pfx = Join-Path $root "..\GameVault\cert\GameVault.pfx"
$pw = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "..\GameVault\pfx_password.ps1")
$work = Join-Path $env:TEMP "xbsx2_resign"; Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$work\packs" | Out-Null
$dist = Join-Path $root "dist"; New-Item -ItemType Directory -Force $dist | Out-Null
foreach ($m in Get-ChildItem (Join-Path $root "bin") -Filter "pcsx2-uwp_${Ver}_*.msix") {
    $dir = Join-Path $work $m.BaseName
    & "$sdk\makeappx.exe" unpack /o /p $m.FullName /d $dir | Out-Null
    Remove-Item "$dir\AppxSignature.p7x", "$dir\AppxBlockMap.xml", "$dir\[Content_Types].xml" -ErrorAction SilentlyContinue
    Remove-Item "$dir\AppxMetadata" -Recurse -ErrorAction SilentlyContinue
    $man = Join-Path $dir "AppxManifest.xml"
    (Get-Content $man -Raw) -replace 'Publisher="CN=SternXD"', 'Publisher="CN=GamerDad"' | Set-Content $man -NoNewline -Encoding UTF8
    $out = Join-Path "$work\packs" $m.Name
    & "$sdk\makeappx.exe" pack /o /d $dir /p $out | Out-Null
    & "$sdk\signtool.exe" sign /fd SHA256 /a /f $pfx /p $pw $out | Out-Null
}
$bundle = Join-Path $dist "XBSX2-for-GameVault_${Ver}_SSE4.msixbundle"
& "$sdk\makeappx.exe" bundle /o /d "$work\packs" /bv "$Ver" /p $bundle | Out-Null
& "$sdk\signtool.exe" sign /fd SHA256 /a /f $pfx /p $pw $bundle | Out-Null
"bundle: $bundle"
