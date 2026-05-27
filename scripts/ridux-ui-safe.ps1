param(
    [string]$BuildDir = "build_codex_check",
    [string]$ActiveIso = ".\build_codex_check\RiduxOS-Unix-firefox-exec-repair-20260505.iso",
    [switch]$Iso,
    [switch]$Boot,
    [switch]$AllowHeavy,
    [switch]$CleanTemps,
    [ValidateSet("xorriso", "grub")]
    [string]$IsoMode = "xorriso",
    [int]$HeavyTimeoutSeconds = 900,
    [int]$MaxOverlayMB = 512,
    [int]$MemoryMB = 1536,
    [int]$CpuCount = 1,
    [int]$CpuExecutionCap = 45,
    [int]$BootSeconds = 25
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

function Quote-Bash {
    param([string]$Text)
    return "'" + ($Text -replace "'", "'\''") + "'"
}

function Invoke-WslSafe {
    param([string]$Command)
    $repoWsl = ConvertTo-WslPath $RepoRoot
    $cmd = "cd $(Quote-Bash $repoWsl) && $Command"
    & wsl bash -lc "$cmd"
    if ($LASTEXITCODE -ne 0) {
        throw "Fallo el comando WSL seguro: $Command"
    }
}

function Assert-NotLocked {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return }
    $stream = $null
    try {
        $stream = [System.IO.File]::Open(
            [System.IO.Path]::GetFullPath($Path),
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None
        )
    } catch {
        throw "El archivo esta en uso o bloqueado: $Path. Cierra VirtualBox/Explorer y volve a probar."
    } finally {
        if ($stream) { $stream.Close() }
    }
}

function Assert-MinSize {
    param(
        [string]$Path,
        [int64]$Bytes,
        [string]$Name
    )
    if (-not (Test-Path $Path)) {
        throw "$Name no existe: $Path"
    }
    $item = Get-Item $Path
    if ($item.Length -lt $Bytes) {
        throw "$Name parece truncado: $Path mide $($item.Length) bytes."
    }
}

function Stop-RiduxVirtualBoxVm {
    param([string]$VmName = "RiduxOS_Codex_UI_Safe")
    $vbox = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
    if (-not (Test-Path $vbox)) { return }

    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $running = & $vbox list runningvms 2>$null
    if ($LASTEXITCODE -eq 0 -and ($running -match "`"$VmName`"")) {
        Write-Host "VM abierta detectada: $VmName. Cerrando para liberar la ISO..."
        & $vbox controlvm $VmName poweroff 2>$null | Out-Null
        Start-Sleep -Seconds 3
    }
    $ErrorActionPreference = $oldEap
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

if ($CleanTemps) {
    Get-ChildItem -Path $BuildDir -File -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -like "*.iso.tmp" -or
            $_.Name -like "*.tmp.iso" -or
            $_.Name -like "*.tmp.iso.tmp"
        } |
        Remove-Item -Force
}

if (($Iso -or $Boot) -and -not $AllowHeavy) {
    throw "Modo protegido: para evitar cuelgues no ejecuto ISO/VM sin confirmacion explicita. Reintenta con -AllowHeavy."
}

$BuildDirWsl = ($BuildDir -replace "\\", "/")
$overlayRel = "$BuildDirWsl/initrd-overlay.img"

function Invoke-MakeSafe {
    param(
        [string]$MakeArgs,
        [int]$TimeoutSeconds = 0
    )

    $runner = "if command -v ionice >/dev/null 2>&1; then ionice -c3 nice -n 10 make $MakeArgs; else nice -n 10 make $MakeArgs; fi"
    if ($TimeoutSeconds -gt 0) {
        $runner = "timeout --kill-after=15s ${TimeoutSeconds}s bash -lc $(Quote-Bash $runner)"
    }

    Invoke-WslSafe $runner
}

Write-Host "== Build liviano Ridux native shell + Ring 3 overlay =="
Invoke-MakeSafe "BUILD_DIR=$(Quote-Bash $BuildDirWsl) r3-apps-only kernel-only $(Quote-Bash $overlayRel) -j1"

$overlayWin = Join-Path $BuildDir "initrd-overlay.img"
Assert-MinSize $overlayWin 1048576 "initrd-overlay.img"
$overlayItem = Get-Item $overlayWin
$overlayMb = [math]::Round($overlayItem.Length / 1MB, 1)
if ($overlayMb -gt $MaxOverlayMB) {
    throw "Overlay demasiado grande para modo seguro: $overlayMb MB (limite: $MaxOverlayMB MB). Usa un rootfs mas chico o sube -MaxOverlayMB sabiendo el riesgo."
}
Write-Host "Overlay listo: $overlayWin"

if ($Iso) {
    $activeFull = [System.IO.Path]::GetFullPath($ActiveIso)
    $initrdWin = Join-Path $BuildDir "initrd.img"
    $newIsoRel = "$BuildDirWsl/RiduxOS-ui-update-new.iso"
    $newIsoWin = Join-Path $BuildDir "RiduxOS-ui-update-new.iso"
    $backupIso = "$activeFull.bak"

    Assert-MinSize $initrdWin 104857600 "initrd.img base"
    Assert-MinSize $activeFull 104857600 "ISO activa"
    Stop-RiduxVirtualBoxVm
    Assert-NotLocked $activeFull

    if (Test-Path $newIsoWin) { Remove-Item -Force $newIsoWin }
    if (Test-Path "$newIsoWin.tmp") { Remove-Item -Force "$newIsoWin.tmp" }
    if (Test-Path "$newIsoWin.xorriso.tmp") { Remove-Item -Force "$newIsoWin.xorriso.tmp" }

    Write-Host "== ISO segura opcional =="
    Write-Host "No se toca la ISO activa hasta que la nueva termine bien."

    if ($IsoMode -eq "xorriso") {
        Write-Host "Modo ISO: xorriso update (menos pesado que recrear todo con grub-mkrescue)."
        Copy-Item -LiteralPath $activeFull -Destination $newIsoWin -Force
        Assert-MinSize $newIsoWin 104857600 "ISO temporal base"
        Invoke-MakeSafe "BUILD_DIR=$(Quote-Bash $BuildDirWsl) ISO=$(Quote-Bash $newIsoRel) iso-xorriso-update -j1" $HeavyTimeoutSeconds
    } else {
        Write-Host "Modo ISO: grub-mkrescue completo."
        Invoke-MakeSafe "BUILD_DIR=$(Quote-Bash $BuildDirWsl) ISO=$(Quote-Bash $newIsoRel) iso-from-existing-initrd -j1" $HeavyTimeoutSeconds
    }

    Assert-MinSize $newIsoWin 104857600 "ISO nueva"
    Assert-NotLocked $activeFull

    if (Test-Path $backupIso) { Remove-Item -Force $backupIso }
    Move-Item -Force $activeFull $backupIso
    try {
        Move-Item -Force $newIsoWin $activeFull
        Remove-Item -Force $backupIso
        Write-Host "ISO activa actualizada: $activeFull"
    } catch {
        if ((Test-Path $backupIso) -and -not (Test-Path $activeFull)) {
            Move-Item -Force $backupIso $activeFull
        }
        throw
    }
}

if ($Boot) {
    Assert-MinSize $ActiveIso 104857600 "ISO para boot"
    Write-Host "== Boot conservador VirtualBox =="
    & powershell -ExecutionPolicy Bypass -File ".\scripts\boot-virtualbox.ps1" `
        -IsoPath $ActiveIso `
        -VmName "RiduxOS_Codex_UI_Safe" `
        -MemoryMB $MemoryMB `
        -CpuCount $CpuCount `
        -CpuExecutionCap $CpuExecutionCap `
        -BootSeconds $BootSeconds `
        -SerialLogPath ".\$BuildDir\vbox-ui-safe.log"
    if ($LASTEXITCODE -ne 0) {
        throw "El boot de VirtualBox fallo."
    }
}

Write-Host "Listo. Para ISO/VM hay modo pesado protegido: usa -Iso/-Boot junto con -AllowHeavy."
