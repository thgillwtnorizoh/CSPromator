param(
    [Parameter(Mandatory=$true)]
    [string]$Cs2GameRoot
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repoRoot "config\gamestate_integration_cspromator.cfg"
$targetDir = Join-Path $Cs2GameRoot "csgo\cfg"
$target = Join-Path $targetDir "gamestate_integration_cspromator.cfg"

if (-not (Test-Path $source)) {
    throw "Missing source config: $source"
}
if (-not (Test-Path $targetDir)) {
    throw "CS2 cfg directory not found: $targetDir. Pass the ...\Counter-Strike Global Offensive\game directory."
}

Copy-Item -Force $source $target
Write-Host "Installed CSPromator GSI config to:"
Write-Host "  $target"
Write-Host "Restart CS2 if it was already running."
