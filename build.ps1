<#
.SYNOPSIS
    Builds the x64dbg_mcp plugin (C++) and the MCP server (C#), and installs the
    plugin into x64dbg's plugins folder.

.PARAMETER X64dbgRoot
    Path to the x64dbg source/checkout root (must contain src/ and bin/).

.PARAMETER Arch
    x64 (default) or x86.

.PARAMETER Config
    Release (default) or Debug.
#>
param(
    [string]$X64dbgRoot = "$PSScriptRoot\..\x64dbg",
    [ValidateSet('x64', 'x86')] [string]$Arch = 'x64',
    [ValidateSet('Release', 'Debug')] [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$cmakeArch = if ($Arch -eq 'x64') { 'x64' } else { 'Win32' }
$binDir = if ($Arch -eq 'x64') { 'x64' } else { 'x32' }

Write-Host "==> Building plugin ($Arch / $Config)..." -ForegroundColor Cyan
$pluginBuild = Join-Path $root "plugin\build-$Arch"
cmake -S "$root\plugin" -B $pluginBuild -G "Visual Studio 17 2022" -A $cmakeArch `
    -DX64DBG_ROOT="$X64dbgRoot" -DINSTALL_TO_X64DBG=ON
cmake --build $pluginBuild --config $Config
if ($LASTEXITCODE -ne 0) { throw "plugin build failed" }

Write-Host "==> Building MCP server (C#)..." -ForegroundColor Cyan
# Output to a stable runtime folder (mcp\app) that is what Claude is registered
# against. NOTE: if a Claude session currently has the server running, the DLL is
# locked and this build will fail with MSB3021 — close the session (or run
# `claude mcp remove x64dbg` and kill stray dotnet.exe) before rebuilding the server.
dotnet build "$root\mcp\x64dbgmcp.csproj" -c $Config -o "$root\mcp\app"
if ($LASTEXITCODE -ne 0) { throw "MCP server build failed (DLL locked by a running Claude session?)" }

$pluginOut = Join-Path $X64dbgRoot "bin\$binDir\plugins\x64dbg_mcp.dp$($Arch -replace 'x','')"
$serverDll = Join-Path $root "mcp\app\x64dbgmcp.dll"

Write-Host ""
Write-Host "Done." -ForegroundColor Green
Write-Host "  Plugin : $pluginOut"
Write-Host "  Server : $serverDll"
Write-Host ""
Write-Host "Register the MCP server with Claude Code:" -ForegroundColor Yellow
Write-Host "  claude mcp add --scope user --transport stdio x64dbg -- dotnet `"$serverDll`""
