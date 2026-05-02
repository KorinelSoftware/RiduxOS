param(
    [string]$IsoPath = ".\build\RiduxOS-Debian-Netinst.iso",
    [string]$SeedIsoPath = ".\build\RiduxOS-Debian-Seed.iso",
    [string]$VmName = "RiduxOS_Debian",
    [string]$DiskPath = ".\build\RiduxOS-Debian.vdi",
    [int]$MemoryMB = 6144,
    [int]$CpuCount = 4,
    [int]$VramMB = 256,
    [int]$DiskGB = 96,
    [switch]$Interactive,
    [switch]$QuickBoot,
    [switch]$Reinstall
)

$ErrorActionPreference = "Stop"
$VBoxManage = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"

if ($QuickBoot -and $Reinstall) {
    throw "No puedes usar -QuickBoot y -Reinstall al mismo tiempo."
}

if (-not (Test-Path $VBoxManage)) {
    throw "VBoxManage no encontrado en '$VBoxManage'."
}

$DiskFullPath = [System.IO.Path]::GetFullPath($DiskPath)
$DiskDir = Split-Path $DiskFullPath -Parent
if (-not (Test-Path $DiskDir)) {
    New-Item -ItemType Directory -Force -Path $DiskDir | Out-Null
}

$DiskExists = Test-Path $DiskFullPath
$AutoQuickBoot = $false
if (-not $QuickBoot -and -not $Reinstall -and $DiskExists) {
    $QuickBoot = $true
    $AutoQuickBoot = $true
}

if ($QuickBoot -and -not $DiskExists) {
    throw "No existe disco instalado en '$DiskFullPath'. Ejecuta sin -QuickBoot para instalar."
}

$IsoFullPath = $null
if (-not $QuickBoot) {
    if (-not (Test-Path $IsoPath)) {
        throw "ISO instalador no encontrada en '$IsoPath'."
    }
    $IsoFullPath = (Resolve-Path $IsoPath).Path
}

$SeedIsoFullPath = $null
if (Test-Path $SeedIsoPath) {
    $SeedIsoFullPath = (Resolve-Path $SeedIsoPath).Path
}

function Invoke-VBox {
    param([string[]]$VBoxArgs)
    & $VBoxManage @VBoxArgs
    if ($LASTEXITCODE -ne 0) {
        throw "VBoxManage fallo con args: $($VBoxArgs -join ' ')"
    }
}

$oldEapProbe = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$null = & $VBoxManage showvminfo $VmName --machinereadable 2>$null
$vmExists = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = $oldEapProbe

if (-not $vmExists) {
    Invoke-VBox @("createvm", "--name", $VmName, "--ostype", "Debian_64", "--register") | Out-Null
}

if (-not (Test-Path $DiskFullPath)) {
    Invoke-VBox @("createmedium", "disk", "--filename", $DiskFullPath, "--size", "$($DiskGB * 1024)", "--format", "VDI") | Out-Null
}

$Boot1 = if ($QuickBoot) { "disk" } else { "dvd" }
$Boot2 = if ($QuickBoot) { "none" } else { "disk" }

Invoke-VBox @(
    "modifyvm", $VmName,
    "--memory", "$MemoryMB",
    "--cpus", "$CpuCount",
    "--vram", "$VramMB",
    "--ioapic", "on",
    "--firmware", "bios",
    "--graphicscontroller", "vmsvga",
    "--mouse", "usbtablet",
    "--boot1", $Boot1,
    "--boot2", $Boot2,
    "--audio-enabled", "off"
) | Out-Null

$storage = & $VBoxManage showvminfo $VmName --machinereadable
if ($LASTEXITCODE -ne 0) {
    throw "No se pudo obtener configuracion de VM."
}

if (-not ($storage -match 'storagecontrollername0="SATA"')) {
    Invoke-VBox @("storagectl", $VmName, "--name", "SATA", "--add", "sata", "--controller", "IntelAhci") | Out-Null
}
if (-not ($storage -match 'storagecontrollername1="IDE"')) {
    Invoke-VBox @("storagectl", $VmName, "--name", "IDE", "--add", "ide", "--controller", "PIIX4") | Out-Null
}

Invoke-VBox @(
    "storageattach", $VmName,
    "--storagectl", "SATA",
    "--port", "0",
    "--device", "0",
    "--type", "hdd",
    "--medium", $DiskFullPath
) | Out-Null

if ($QuickBoot) {
    Invoke-VBox @(
        "storageattach", $VmName,
        "--storagectl", "IDE",
        "--port", "0",
        "--device", "0",
        "--type", "dvddrive",
        "--medium", "none"
    ) | Out-Null
} else {
    Invoke-VBox @(
        "storageattach", $VmName,
        "--storagectl", "IDE",
        "--port", "0",
        "--device", "0",
        "--type", "dvddrive",
        "--medium", $IsoFullPath
    ) | Out-Null
}

if ($SeedIsoFullPath) {
    Invoke-VBox @(
        "storageattach", $VmName,
        "--storagectl", "IDE",
        "--port", "1",
        "--device", "0",
        "--type", "dvddrive",
        "--medium", $SeedIsoFullPath
    ) | Out-Null
} else {
    Invoke-VBox @(
        "storageattach", $VmName,
        "--storagectl", "IDE",
        "--port", "1",
        "--device", "0",
        "--type", "dvddrive",
        "--medium", "none"
    ) | Out-Null
}

$oldEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $VBoxManage controlvm $VmName poweroff 2>$null | Out-Null
$ErrorActionPreference = $oldEap
Start-Sleep -Seconds 2

if ($Interactive) {
    Invoke-VBox @("startvm", $VmName, "--type", "gui") | Out-Null
} else {
    Invoke-VBox @("startvm", $VmName, "--type", "headless") | Out-Null
}

Write-Output "VM arrancada: $VmName"
Write-Output "Disco: $DiskFullPath"
if ($SeedIsoFullPath) {
    Write-Output "Seed ISO: $SeedIsoFullPath"
}

if ($QuickBoot) {
    Write-Output "Modo: quick boot (disco)."
    if ($AutoQuickBoot) {
        Write-Output "Auto quick boot activo: usa -Reinstall para forzar instalacion desde ISO."
    }
    return
}

Write-Output "ISO instalador: $IsoFullPath"
Write-Output ""
Write-Output "Pasos dentro de Debian (despues de instalar y arrancar desde disco):"
Write-Output "  1) monta el Seed ISO (si no auto-monta)"
Write-Output "  2) ejecuta:"
Write-Output "     sudo bash /media/cdrom/setup_debian_ridux_runtime.sh --user ridux --fast"
Write-Output "  3) reinicia"
