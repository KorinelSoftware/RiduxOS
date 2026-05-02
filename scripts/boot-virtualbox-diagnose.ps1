param(
    [string]$IsoPath = ".\build\RiduxOS-Unix.iso",
    [string]$VmName = "RiduxOS_Unix_Demo",
    [int]$MemoryMB = 2048,
    [int]$CpuCount = 2,
    [int]$CpuExecutionCap = 65,
    [int]$VramMB = 128,
    [int]$BootSeconds = 30,
    [int]$StartTimeoutSeconds = 60,
    [int]$TailLines = 220,
    [switch]$TraceWait,
    [switch]$Headless,
    [switch]$KeepRunning,
    [switch]$NoBootMenuEnter
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
$IsoInfo = Get-Item $IsoFullPath
if ($IsoInfo.Length -lt 100MB) {
    throw "ISO parece truncada/corrupta: '$IsoFullPath' mide $($IsoInfo.Length) bytes."
}
$BuildDir = Split-Path $IsoFullPath -Parent
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$SerialFullPath = Join-Path $BuildDir ("vbox-serial-" + $Stamp + ".log")
$ScreenshotPath = Join-Path $BuildDir ("vbox-diag-" + $Stamp + ".png")
$SummaryPath = Join-Path $BuildDir ("vbox-diag-" + $Stamp + ".txt")

function Invoke-VBox {
    param([string[]]$VBoxArgs)
    & $VBoxManage @VBoxArgs
    if ($LASTEXITCODE -ne 0) {
        throw "VBoxManage fallo con args: $($VBoxArgs -join ' ')"
    }
}

function Wait-ForVmRunning {
    param([string]$Name, [int]$TimeoutSeconds)
    $i = 0
    while ($i -lt $TimeoutSeconds) {
        Start-Sleep -Seconds 1
        $info = & $VBoxManage showvminfo $Name --machinereadable
        if ($TraceWait) {
            $stateLine = ($info | Select-String -Pattern '^VMState=' | Select-Object -First 1).Line
            Write-Host ("[wait-running] t=" + $i + " state=" + $stateLine)
        }
        if ($LASTEXITCODE -eq 0 -and ($info -match 'VMState="running"')) {
            return [bool]$true
        }
        $i++
    }
    return [bool]$false
}

function Wait-ForVmPoweredOff {
    param([string]$Name, [int]$TimeoutSeconds)
    $i = 0
    while ($i -lt $TimeoutSeconds) {
        Start-Sleep -Seconds 1
        $info = & $VBoxManage showvminfo $Name --machinereadable
        if ($TraceWait) {
            $stateLine = ($info | Select-String -Pattern '^VMState=' | Select-Object -First 1).Line
            Write-Host ("[wait-off] t=" + $i + " state=" + $stateLine)
        }
        if ($LASTEXITCODE -eq 0 -and -not ($info -match 'VMState="running"')) {
            return [bool]$true
        }
        $i++
    }
    return [bool]$false
}

$vms = & $VBoxManage list vms
if ($LASTEXITCODE -ne 0) {
    throw "No se pudo listar VMs."
}

if (-not ($vms -match "`"$VmName`"")) {
    Invoke-VBox @("createvm", "--name", $VmName, "--ostype", "Other_64", "--register") | Out-Null
    Invoke-VBox @("storagectl", $VmName, "--name", "IDE", "--add", "ide", "--controller", "PIIX4") | Out-Null
}

# If already running, stop it before applying VM config.
$oldEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $VBoxManage controlvm $VmName poweroff 2>$null | Out-Null
$ErrorActionPreference = $oldEap
[void](Wait-ForVmPoweredOff -Name $VmName -TimeoutSeconds 10)

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
    "--graphicscontroller", "vboxsvga",
    "--mouse", "ps2",
    "--keyboard", "ps2",
    "--boot1", "dvd",
    "--audio-enabled", "off"
) | Out-Null

Invoke-VBox @(
    "modifyvm", $VmName,
    "--uart1", "0x3F8", "4",
    "--uart-mode1", "file", $SerialFullPath
) | Out-Null

# Confirm UART really points to this run's file. VBox can silently keep
# the previous path when args are malformed; fail fast if it does.
$vmInfoUart = & $VBoxManage showvminfo $VmName --machinereadable
$uartModeLine = ($vmInfoUart | Select-String -Pattern '^uartmode1=' | Select-Object -First 1).Line
if (-not $uartModeLine -or ($uartModeLine -notmatch [regex]::Escape($SerialFullPath))) {
    Invoke-VBox @("modifyvm", $VmName, "--uart-mode1", "file", $SerialFullPath) | Out-Null
    $vmInfoUart = & $VBoxManage showvminfo $VmName --machinereadable
    $uartModeLine = ($vmInfoUart | Select-String -Pattern '^uartmode1=' | Select-Object -First 1).Line
    if (-not $uartModeLine -or ($uartModeLine -notmatch [regex]::Escape($SerialFullPath))) {
        throw "No se pudo fijar uartmode1 al serial log esperado: $SerialFullPath (actual: $uartModeLine)"
    }
}

if (Test-Path $SerialFullPath) {
    Remove-Item -Force $SerialFullPath
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

$startType = if ($Headless) { "headless" } else { "gui" }
$startOut = & $VBoxManage startvm $VmName --type $startType
if ($TraceWait -and $startOut) {
    $startOut | ForEach-Object { Write-Host ("[startvm] " + $_) }
}
if ($LASTEXITCODE -ne 0) {
    throw "VBoxManage fallo al iniciar VM (startvm)."
}
if (-not (Wait-ForVmRunning -Name $VmName -TimeoutSeconds $StartTimeoutSeconds)) {
    throw "La VM no quedo en ejecucion despues de startvm (timeout ${StartTimeoutSeconds}s)."
}

# GRUB in this ISO uses timeout=0, so injecting Enter after boot can land in
# the Ridux shell and accidentally re-run the previous command. Just wait.
Start-Sleep -Seconds $BootSeconds

$shotOk = $false
for ($shotTry = 0; $shotTry -lt 3; $shotTry++) {
    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $VBoxManage controlvm $VmName screenshotpng $ScreenshotPath 2>$null | Out-Null
    $ErrorActionPreference = $oldEap
    if (Test-Path $ScreenshotPath -PathType Leaf) {
        if ((Get-Item $ScreenshotPath).Length -gt 0) {
            $shotOk = $true
            break
        }
    }
    Start-Sleep -Seconds 2
}
if (-not $shotOk) {
    throw "No se pudo generar screenshot: $ScreenshotPath"
}

$serialTail = @()
if (Test-Path $SerialFullPath) {
    $serialTail = Get-Content $SerialFullPath | Select-Object -Last $TailLines
}

$panicLines = @()
if ($serialTail.Count -gt 0) {
    $panicLines = $serialTail | Where-Object {
        $_ -match "PANIC:" -or $_ -match "\[!!!\]" -or $_ -match "kernel panic" -or $_ -match "exception vec="
    }
}

@(
    "RiduxOS VM diagnose run: $Stamp"
    "VM: $VmName"
    "ISO: $IsoFullPath"
    "Serial log: $SerialFullPath"
    "Screenshot: $ScreenshotPath"
    ""
    "----- Panic/Exception matches -----"
) + ($panicLines | ForEach-Object { $_ }) + @(
    ""
    "----- Serial tail ($TailLines lines) -----"
) + ($serialTail | ForEach-Object { $_ }) | Set-Content -Path $SummaryPath -Encoding UTF8

if (-not $KeepRunning) {
    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $VBoxManage controlvm $VmName poweroff 2>$null | Out-Null
    $ErrorActionPreference = $oldEap
}

Write-Output "VM diagnose listo."
Write-Output "Screenshot: $ScreenshotPath"
Write-Output "Serial log : $SerialFullPath"
Write-Output "Summary    : $SummaryPath"
if ($panicLines.Count -gt 0) {
    Write-Output "Detectado panic/exception en el log:"
    $panicLines | ForEach-Object { Write-Output ("  " + $_) }
} else {
    Write-Output "No se detectaron lineas de panic en el tail capturado."
}
