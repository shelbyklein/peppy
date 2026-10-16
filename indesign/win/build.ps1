# Peppy for InDesign on Windows: build (and optionally install) Peppy.pln.
#   powershell -ExecutionPolicy Bypass -File indesign\win\build.ps1 [-Install] [-Sdk C:\sdk\AdobeInDesignSDK] [-InDesign "C:\Program Files\Adobe\Adobe InDesign 2026"]
# Needs Visual Studio 2022 (Build Tools is enough) with the C++ workload and the
# Windows 11 SDK 10.0.26100, and the InDesign 2026 plug-in SDK.
param(
	[string]$Sdk = "C:\sdk\AdobeInDesignSDK",
	[string]$InDesign = "",
	[switch]$Install
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Gen = Join-Path $Root "build\win"
New-Item -ItemType Directory -Force $Gen | Out-Null

# The version, from the repo's VERSION file (the only copy).
$Version = (Get-Content (Join-Path $Root "VERSION") -Raw).Trim()
Set-Content -Encoding ascii (Join-Path $Gen "PpyVersion.h") "#pragma once`r`n#define kPpyVersion `"$Version`""

# peppy.jsx as C string literals, one per line (MSVC refuses one literal over 64 KB).
$lines = (Get-Content (Join-Path $Root "indesign\peppy.jsx") -Raw -Encoding utf8) -split "`r?`n"
$out = foreach ($l in $lines) {
	$e = $l.Replace('\', '\\').Replace('"', '\"').Replace("`t", '\t')
	"`"$e\n`","
}
Set-Content -Encoding utf8 (Join-Path $Gen "peppy.jsx.inc") $out

# The SDK's property sheets use paths relative to build\win\prj, so the project goes there.
$Prj = Join-Path $Sdk "build\win\prj"
Copy-Item -Force (Join-Path $PSScriptRoot "Peppy.vcxproj") $Prj

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild not found: install Visual Studio 2022 Build Tools with the C++ workload" }
& $msbuild (Join-Path $Prj "Peppy.vcxproj") /nologo /m /v:minimal /p:Configuration=Release /p:Platform=x64 "/p:PeppyRoot=$Root" "/p:PeppyVersion=$Version"
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# Peppy.pln and its "(Peppy Resources)" folder, gathered into build\win\Peppy.
$Built = Join-Path $Sdk "build\win\releasex64\peppy"
$Dist = Join-Path $Gen "Peppy"
if (Test-Path $Dist) { Remove-Item -Recurse -Force $Dist }
New-Item -ItemType Directory -Force $Dist | Out-Null
Copy-Item (Join-Path $Built "Peppy.pln") $Dist
Copy-Item -Recurse (Join-Path $Built "(Peppy Resources)") $Dist
Write-Host "Built $Dist"

if ($Install) {
	if (-not $InDesign) {
		$InDesign = Get-ChildItem "$env:ProgramFiles\Adobe" -Directory -Filter "Adobe InDesign 2026*" | Select-Object -First 1 -ExpandProperty FullName
	}
	if (Get-Process -Name "InDesign*" -ErrorAction SilentlyContinue) { throw "Quit InDesign first, then install." }
	$Target = Join-Path $InDesign "Plug-Ins\Peppy"
	if (Test-Path $Target) { Remove-Item -Recurse -Force $Target }
	Copy-Item -Recurse $Dist $Target
	Write-Host "Installed into $Target"
}
