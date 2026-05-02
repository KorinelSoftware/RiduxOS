param(
    # Ridux production ISO. Defaults to the new Ridux-branded artifact;
    # we still accept the legacy RiduxOS-FreeBSD-Browser.iso name as a
    # fallback for in-flight VMs with old paths cached.
    [string]$IsoPath = ".\build\RiduxOS-Browser.iso",
    [string]$VmName = "RiduxOS_Ridux",
    [string]$DiskPath = ".\build\RiduxOS-Ridux.vdi",
    [int]$MemoryMB = 6144,
    [int]$CpuCount = 4,
    [int]$VramMB = 256,
    [int]$DiskGB = 80,
    [switch]$Interactive,
    [switch]$KeepIsoAttached,
    [int]$InstallWaitMinutes = 35,
    [switch]$QuickBoot,
    [switch]$Reinstall
)

# Backwards compat: if caller did not pass an explicit ISO and the
# new-named one is missing, fall back to the legacy artifact name.
if (-not $PSBoundParameters.ContainsKey('IsoPath') -and -not (Test-Path $IsoPath)) {
    $legacyIso = ".\build\RiduxOS-FreeBSD-Browser.iso"
    if (Test-Path $legacyIso) {
        Write-Host "[boot-ridux-vm] using legacy ISO: $legacyIso"
        $IsoPath = $legacyIso
    }
}

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
        throw "ISO no encontrada en '$IsoPath'."
    }
    $IsoFullPath = (Resolve-Path $IsoPath).Path
}

function Invoke-VBox {
    param([string[]]$VBoxArgs)
    & $VBoxManage @VBoxArgs
    if ($LASTEXITCODE -ne 0) {
        throw "VBoxManage fallo con args: $($VBoxArgs -join ' ')"
    }
}

function Invoke-VBoxOptional {
    param([string[]]$VBoxArgs)
    & $VBoxManage @VBoxArgs 2>$null | Out-Null
    return ($LASTEXITCODE -eq 0)
}

$oldEapProbe = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$null = & $VBoxManage showvminfo $VmName --machinereadable 2>$null
$vmExists = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = $oldEapProbe

if (-not $vmExists) {
    Invoke-VBox @("createvm", "--name", $VmName, "--ostype", "FreeBSD_64", "--register") | Out-Null
}

$hdds = & $VBoxManage list hdds
if ($LASTEXITCODE -ne 0) {
    throw "No se pudo listar discos de VirtualBox."
}

if (-not (Test-Path $DiskFullPath)) {
    Invoke-VBox @("createmedium", "disk", "--filename", $DiskFullPath, "--size", "$($DiskGB * 1024)", "--format", "VDI") | Out-Null
}

$oldEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $VBoxManage controlvm $VmName poweroff 2>$null | Out-Null
$ErrorActionPreference = $oldEap
Start-Sleep -Seconds 2

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
    "--nic1", "nat",
    "--nictype1", "82540EM",
    "--cableconnected1", "on",
    "--natdnsproxy1", "on",
    "--natdnshostresolver1", "on",
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
    $detached = Invoke-VBoxOptional @(
        "storageattach", $VmName,
        "--storagectl", "IDE",
        "--port", "0",
        "--device", "0",
        "--type", "dvddrive",
        "--medium", "none"
    )
    if (-not $detached) {
        [void](Invoke-VBoxOptional @(
            "storageattach", $VmName,
            "--storagectl", "IDE",
            "--port", "0",
            "--device", "0",
            "--type", "dvddrive",
            "--medium", "emptydrive"
        ))
    }
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

if ($QuickBoot) {
    Write-Output "Modo: quick boot (disco)."
    if ($AutoQuickBoot) {
        Write-Output "Auto quick boot activo: usa -Reinstall para forzar instalacion desde ISO."
    }
    return
}

Write-Output "ISO: $IsoFullPath"

if ($KeepIsoAttached) {
    Write-Output "Modo instalador: deja correr la instalacion automatica."
    Write-Output "Cuando termine, apaga VM, desmonta ISO y arranca desde disco."
    return
}

if ($InstallWaitMinutes -lt 5) {
    $InstallWaitMinutes = 5
}

Write-Output "Esperando $InstallWaitMinutes minutos para instalacion automatica..."
Start-Sleep -Seconds ($InstallWaitMinutes * 60)

$ErrorActionPreference = "Continue"
& $VBoxManage controlvm $VmName acpipowerbutton 2>$null | Out-Null
Start-Sleep -Seconds 20
& $VBoxManage controlvm $VmName poweroff 2>$null | Out-Null
$ErrorActionPreference = $oldEap
Start-Sleep -Seconds 2

Invoke-VBox @(
    "storageattach", $VmName,
    "--storagectl", "IDE",
    "--port", "0",
    "--device", "0",
    "--type", "dvddrive",
    "--medium", "none"
) | Out-Null

Invoke-VBox @("modifyvm", $VmName, "--boot1", "disk", "--boot2", "none") | Out-Null

if ($Interactive) {
    Invoke-VBox @("startvm", $VmName, "--type", "gui") | Out-Null
} else {
    Invoke-VBox @("startvm", $VmName, "--type", "headless") | Out-Null
}

Write-Output "ISO desmontada y VM relanzada desde disco."
