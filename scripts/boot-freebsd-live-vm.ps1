param(
    [string]$IsoPath = ".\build\RiduxOS-Live.iso",
    [string]$VmName = "RiduxOS_Live",
    [int]$MemoryMB = 8192,
    [int]$CpuCount = 4,
    [int]$VramMB = 256,
    [switch]$Headless
)

$ErrorActionPreference = "Stop"
$VBoxManage = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"

if (-not (Test-Path $VBoxManage)) {
    throw "VBoxManage no encontrado en '$VBoxManage'."
}
if (-not (Test-Path $IsoPath)) {
    throw "ISO no encontrada en '$IsoPath'."
}

$IsoFullPath = (Resolve-Path $IsoPath).Path

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
    Invoke-VBox @("createvm", "--name", $VmName, "--platform-architecture", "x86", "--ostype", "FreeBSD_64", "--register") | Out-Null
}

$oldEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $VBoxManage controlvm $VmName poweroff 2>$null | Out-Null
$ErrorActionPreference = $oldEap
Start-Sleep -Seconds 2

Invoke-VBox @(
    "modifyvm", $VmName,
    "--memory", "$MemoryMB",
    "--cpus", "$CpuCount",
    "--vram", "$VramMB",
    "--ioapic", "on",
    "--firmware", "bios",
    "--graphicscontroller", "vboxvga",
    "--mouse", "usbtablet",
    "--nic1", "nat",
    "--nictype1", "82540EM",
    "--cableconnected1", "on",
    "--natdnsproxy1", "on",
    "--natdnshostresolver1", "on",
    "--boot1", "dvd",
    "--boot2", "none",
    "--boot3", "none",
    "--boot4", "none",
    "--audio-enabled", "off"
) | Out-Null

$storage = & $VBoxManage showvminfo $VmName --machinereadable
if ($LASTEXITCODE -ne 0) {
    throw "No se pudo obtener configuracion de VM."
}

if (-not ($storage -match 'storagecontrollername0="IDE"')) {
    Invoke-VBox @("storagectl", $VmName, "--name", "IDE", "--add", "ide", "--controller", "PIIX4") | Out-Null
}
if (-not ($storage -match 'storagecontrollername1="SATA"')) {
    Invoke-VBox @("storagectl", $VmName, "--name", "SATA", "--add", "sata", "--controller", "IntelAhci") | Out-Null
}

$oldEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $VBoxManage controlvm $VmName poweroff 2>$null | Out-Null
$ErrorActionPreference = $oldEap
Start-Sleep -Seconds 2

Invoke-VBox @(
    "storageattach", $VmName,
    "--storagectl", "IDE",
    "--port", "0",
    "--device", "0",
    "--type", "dvddrive",
    "--medium", $IsoFullPath
) | Out-Null

if ($Headless) {
    Invoke-VBox @("startvm", $VmName, "--type", "headless") | Out-Null
    $mode = "headless"
} else {
    Invoke-VBox @("startvm", $VmName, "--type", "gui") | Out-Null
    $mode = "gui"
}

Write-Output "VM Live arrancada: $VmName"
Write-Output "ISO: $IsoFullPath"
Write-Output "Modo: $mode"
