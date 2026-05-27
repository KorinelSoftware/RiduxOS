param(
    # Default to the canonical repo-root ISO produced by the native Ridux shell build.
    # Older parallel ISOs in build_*\*.iso are still picked up via the
    # auto-detection block below if the canonical one is missing, so existing
    # workflows keep working.
    [string]$IsoPath = ".\RiduxOS.iso",
    [string]$VmName = "RiduxOS_Native_Desktop",
    [int]$MemoryMB = 2048,
    [int]$CpuCount = 2,
    [int]$CpuExecutionCap = 65,
    [int]$VramMB = 128,
    [int]$BootSeconds = 30,
    [string]$SerialLogPath = ".\build\vbox-serial.log",
    [string]$GraphicsController = "vmsvga",
    [ValidateSet("usbtablet", "ps2", "usb")]
    [string]$Mouse = "ps2",
    [switch]$Accelerate3D = $true,
    [switch]$SkipScreenshot,
    [switch]$KeepRunning,
    [switch]$Interactive
)

$ErrorActionPreference = "Stop"
$VBoxManage = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"

if (-not (Test-Path $VBoxManage)) {
    throw "VBoxManage no encontrado en '$VBoxManage'."
}
if (-not (Test-Path $IsoPath)) {
    # Auto-detect the most recently built ISO if the requested path is missing.
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $candidates = @(
        (Join-Path $repoRoot "RiduxOS.iso"),
        (Join-Path $repoRoot "build_wayfire_safe\RiduxOS-Wayfire-Full-Desktop.iso"),
        (Join-Path $repoRoot "build_wsl_firefox\RiduxOS-Unix.iso"),
        (Join-Path $repoRoot "build_codex_check\RiduxOS-Unix-firefox-exec-repair-20260505.iso")
    )
    $resolved = $null
    foreach ($cand in $candidates) {
        if (Test-Path $cand) { $resolved = $cand; break }
    }
    if (-not $resolved) {
        $resolved = Get-ChildItem -Path $repoRoot -Recurse -Filter "*.iso" -ErrorAction SilentlyContinue |
            Where-Object { $_.Length -gt 100MB } |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1 -ExpandProperty FullName
    }
    if ($resolved) {
        Write-Output "ISO '$IsoPath' no encontrada; usando '$resolved'."
        $IsoPath = $resolved
    } else {
        throw "ISO no encontrada en '$IsoPath' (ni en build_*\*.iso). Genera una con make all o con .\scripts\ridux-ui-safe.ps1 -Iso -AllowHeavy."
    }
}

$IsoFullPath = (Resolve-Path $IsoPath).Path
$IsoInfo = Get-Item $IsoFullPath
if ($IsoInfo.Length -lt 100MB) {
    throw "ISO parece truncada/corrupta: '$IsoFullPath' mide $($IsoInfo.Length) bytes."
}
$ScreenshotPath = Join-Path (Split-Path $IsoFullPath -Parent) "vbox-boot.png"
$SerialFullPath = [System.IO.Path]::GetFullPath($SerialLogPath)

function Invoke-VBox {
    param([string[]]$VBoxArgs)
    & $VBoxManage @VBoxArgs
    if ($LASTEXITCODE -ne 0) {
        throw "VBoxManage fallo con args: $($VBoxArgs -join ' ')"
    }
}

$vms = & $VBoxManage list vms
if ($LASTEXITCODE -ne 0) {
    throw "No se pudo listar VMs."
}

$vmExists = $vms -match "`"$VmName`""
if (-not $vmExists) {
    Invoke-VBox @("createvm", "--name", $VmName, "--ostype", "Other_64", "--register") | Out-Null
    Invoke-VBox @("storagectl", $VmName, "--name", "IDE", "--add", "ide", "--controller", "PIIX4") | Out-Null
}

# If already running, stop it before applying VM config.
$oldEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $VBoxManage controlvm $VmName poweroff 2>$null | Out-Null
$ErrorActionPreference = $oldEap
Start-Sleep -Seconds 2

Invoke-VBox @(
    "modifyvm", $VmName,
    "--os-type", "Other_64",
    "--ioapic", "on",
    "--x86-pae", "on",
    "--x86-long-mode", "on",
    "--memory", "$MemoryMB",
    "--cpus", "$CpuCount",
    "--cpuexecutioncap", "$CpuExecutionCap",
    "--vram", "$VramMB",
    "--graphicscontroller", $GraphicsController,
    "--accelerate3d", $(if ($Accelerate3D) { "on" } else { "off" }),
    "--mouse", $Mouse,
    "--keyboard", "ps2",
    "--nic1", "nat",
    "--nictype1", "82540EM",
    "--cableconnected1", "on",
    "--boot1", "dvd",
    "--audio-enabled", "off"
) | Out-Null

if (Test-Path $SerialFullPath) {
    Remove-Item -Force $SerialFullPath
}
Invoke-VBox @(
    "modifyvm", $VmName,
    "--uart1", "0x3F8", "4",
    "--uart-mode1", "file", $SerialFullPath
) | Out-Null

# Verify UART log target to avoid reading stale logs from previous runs (only if VM existed before)
if ($vmExists) {
    $vmInfoUart = & $VBoxManage showvminfo $VmName --machinereadable
    $uartModeLine = ($vmInfoUart | Select-String -Pattern '^uartmode1=' | Select-Object -First 1).Line
    if ($uartModeLine -and ($uartModeLine -notmatch [regex]::Escape($SerialFullPath))) {
        throw "No se pudo fijar uartmode1 al log esperado: $SerialFullPath (actual: $uartModeLine)"
    }
}
New-Item -ItemType File -Path $SerialFullPath -Force | Out-Null

& $VBoxManage setextradata $VmName "CustomVideoMode1" "1280x720x32" | Out-Null
& $VBoxManage setextradata $VmName "CustomVideoMode2" "1600x900x32" | Out-Null
& $VBoxManage setextradata $VmName "CustomVideoMode3" "1920x1080x32" | Out-Null

Invoke-VBox @(
    "storageattach", $VmName,
    "--storagectl", "IDE",
    "--port", "0",
    "--device", "0",
    "--type", "dvddrive",
    "--medium", $IsoFullPath
) | Out-Null

if (Test-Path $ScreenshotPath) {
    Remove-Item -Force $ScreenshotPath
}

if ($Interactive) {
    Invoke-VBox @("startvm", $VmName, "--type", "gui") | Out-Null
} else {
    Invoke-VBox @("startvm", $VmName, "--type", "headless") | Out-Null
}
Start-Sleep -Milliseconds 800
$runningNow = & $VBoxManage list runningvms
if ($LASTEXITCODE -ne 0 -or -not ($runningNow -match "`"$VmName`"")) {
    throw "La VM no quedo en ejecucion despues de startvm."
}

if ($SkipScreenshot) {
    if (-not $KeepRunning) {
        Start-Sleep -Seconds 10
        Invoke-VBox @("controlvm", $VmName, "poweroff") | Out-Null
        Write-Output "VM arrancada correctamente y apagada sin screenshot: $VmName"
    } else {
        Write-Output "VM arrancada correctamente y dejada en ejecucion: $VmName"
    }
    Write-Output "Serial log: $SerialFullPath"
    return
}

Start-Sleep -Seconds $BootSeconds

Invoke-VBox @("controlvm", $VmName, "screenshotpng", $ScreenshotPath) | Out-Null
Start-Sleep -Seconds 1
if (-not $KeepRunning) {
    Invoke-VBox @("controlvm", $VmName, "poweroff") | Out-Null
}

if (-not (Test-Path $ScreenshotPath) -or (Get-Item $ScreenshotPath).Length -le 0) {
    throw "No se pudo generar screenshot en '$ScreenshotPath'."
}

Write-Output "VM arrancada correctamente. Captura: $ScreenshotPath"
Write-Output "Serial log: $SerialFullPath"
if ($KeepRunning) {
    Write-Output "VM sigue en ejecucion porque se paso -KeepRunning."
}
