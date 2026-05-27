param(
    [string]$BuildDir = "build_plasma_gpu",
    [string]$OutputIso = ".\RiduxOS.iso",
    [switch]$Boot,
    [switch]$Qemu,
    [switch]$FetchPlasmaDebs,
    [ValidateSet("virtio", "vbox", "vmwgfx", "svga")]
    [string]$GpuKind = "virtio",
    [int]$TimeoutSeconds = 1200,
    [int]$MaxInitrdMB = 2200,
    [int]$MaxOverlayMB = 1800,
    [int]$MaxIsoMB = 2400
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

function ConvertTo-WslPath {
    param([string]$Path)
    $full = [System.IO.Path]::GetFullPath($Path)
    if ($full -match "^([A-Za-z]):\\(.*)$") {
        $drive = $Matches[1].ToLowerInvariant()
        $rest = $Matches[2] -replace "\\", "/"
        return "/mnt/$drive/$rest"
    }
    $out = & wsl wslpath -a "$full"
    if ($LASTEXITCODE -ne 0 -or -not $out) {
        throw "No pude convertir a path WSL: $Path"
    }
    return ($out | Select-Object -First 1).Trim()
}

function ConvertTo-BashQuoted {
    param([string]$Text)
    return "'" + ($Text -replace "'", "'\''") + "'"
}

function Invoke-WslInRepo {
    param([string]$Command)
    $repoWsl = ConvertTo-WslPath $RepoRoot
    & wsl bash -lc "cd $(ConvertTo-BashQuoted $repoWsl) && $Command"
    if ($LASTEXITCODE -ne 0) {
        throw "Fallo comando WSL: $Command"
    }
}

Write-Host "== RiduxOS KDE Plasma GPU =="
Write-Host "GPU kind: $GpuKind"

$fetchFlag = if ($FetchPlasmaDebs) { "1" } else { "0" }
Invoke-WslInRepo "RIDUX_PLASMA_GPU=$(ConvertTo-BashQuoted $GpuKind) RIDUX_FETCH_DEBIAN_PLASMA=$fetchFlag make plasma-rootfs"

$scriptArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", ".\scripts\ridux-wayfire-safe.ps1",
    "-BuildDir", $BuildDir,
    "-OutputIso", $OutputIso,
    "-EnableGpuRenderer",
    "-FetchDesktopStack",
    "-MaxInitrdMB", "$MaxInitrdMB",
    "-MaxOverlayMB", "$MaxOverlayMB",
    "-MaxIsoMB", "$MaxIsoMB",
    "-TimeoutSeconds", "$TimeoutSeconds"
)
if ($Boot) { $scriptArgs += "-Boot" }
if ($Qemu) { $scriptArgs += "-Qemu" }

Write-Host "== ISO Plasma/GPU =="
& powershell @scriptArgs
if ($LASTEXITCODE -ne 0) {
    throw "Fallo build de ISO Plasma/GPU."
}

Write-Host "Listo: KDE Plasma queda como shell preferida; Wayfire queda fallback."
