param(
    [string]$IsoPath = ".\build_codex_gpu\RiduxOS-Unix.iso",
    [int]$MemoryMB = 4096,
    [int]$CpuCount = 4,
    [int]$BootSeconds = 180,
    [int]$MonitorPort = 45454,
    [string]$SerialLogPath = ".\build_wayfire_safe\qemu-wayfire-safe.log",
    [string]$ScreenshotPath = ".\build_wayfire_safe\qemu-boot.png",
    [string]$Machine = "q35",
    [string]$CpuModel = "qemu64",
    [string]$GpuDevice = "virtio-vga-gl,blob=true,venus=true,hostmem=512M,xres=1024,yres=768,id=riduxgpu",
    [string]$Vga = "none",
    [string]$Display = "",
    [ValidateSet("Normal", "AboveNormal", "High")]
    [string]$ProcessPriority = "High",
    [switch]$Interactive,
    [switch]$KeepRunning,
    [switch]$UseHostQemu,
    [switch]$UseWslQemu,
    [switch]$RequireVirgl,
    [switch]$RequireVulkan,
    [switch]$RequireGpuLadder
)

$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

if (-not (Test-Path $IsoPath)) {
    throw "ISO no encontrada en '$IsoPath'."
}

$IsoFullPath = (Resolve-Path $IsoPath).Path
$IsoInfo = Get-Item $IsoFullPath
if ($IsoInfo.Length -lt 100MB) {
    throw "ISO parece truncada/corrupta: '$IsoFullPath' mide $($IsoInfo.Length) bytes."
}

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

function Convert-QemuScreendumpIfNeeded {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 2 -or $bytes[0] -ne 0x50 -or $bytes[1] -ne 0x36) { return }
    if ([System.IO.Path]::GetExtension($Path).ToLowerInvariant() -ne ".png") { return }

    $ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if (-not $ffmpeg) {
        Write-Warning "QEMU genero PPM aunque la extension sea .png, y ffmpeg no esta disponible para convertirlo."
        return
    }

    $ppmPath = [System.IO.Path]::ChangeExtension($Path, ".ppm")
    Move-Item -LiteralPath $Path -Destination $ppmPath -Force
    & $ffmpeg.Source -y -loglevel error -i $ppmPath $Path
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $Path)) {
        Move-Item -LiteralPath $ppmPath -Destination $Path -Force
        throw "QEMU genero PPM, pero no pude convertirlo a PNG: $Path"
    }
}

function Test-SerialLogHasVirglGpu {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $false }
    $hasVirgl = Select-String -LiteralPath $Path -Pattern "GL renderer: virgl", "renderer=.*virgl", "\[ridux-mesa-real\].*virgl", "\[virgl-submit\]" -Quiet
    $hasProductionVirgl = (
        (Select-String -LiteralPath $Path -Pattern "GALLIUM_DRIVER=virgl" -Quiet) -and
        (Select-String -LiteralPath $Path -Pattern "\[virtgpu-capset\].*id=2.*rc=0" -Quiet)
    )
    $hasVirtgpuVirglCapset = Select-String -LiteralPath $Path -Pattern "\[virtgpu-capset\].*id=2.*rc=0" -Quiet
    $hasSoftware = Select-String -LiteralPath $Path -Pattern "LLVMPIPE", "llvmpipe", "softpipe", "swrast", "lavapipe", "software rasterizer", "rejected software" -Quiet
    return (($hasVirgl -or $hasProductionVirgl -or $hasVirtgpuVirglCapset) -and -not $hasSoftware)
}

function Test-SerialLogHasGpuLadder {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $false }
    return (Select-String -LiteralPath $Path -Pattern "\[ridux-gpu-ladder\] overall=ok" -Quiet)
}

function Test-SerialLogHasVulkanGpu {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $false }
    $hasVulkanMarker = Select-String -LiteralPath $Path -Pattern "\[ridux-vulkan-real\].*hardware-required accepted" -Quiet
    $hasProbeExitOk = Select-String -LiteralPath $Path -Pattern "\[exit!\]\s+pid=\d+\s+code=0\s+name=ridux-vulkan-probe", "\[task-exit!\]\s+pid=\d+\s+code=0.*name=ridux-vulkan-probe" -Quiet
    $hasHardwareVenus = Select-String -LiteralPath $Path -Pattern "Virtio-GPU Venus \(Microsoft Direct3D12", "Virtio-GPU Venus \(.*NVIDIA", "Virtio-GPU Venus \(.*AMD", "Virtio-GPU Venus \(.*Intel" -Quiet
    $hasSoftwareOnly = Select-String -LiteralPath $Path -Pattern "rejected software/no-graphics Vulkan", "Virtio-GPU Venus .*llvmpipe", "llvmpipe", "lavapipe", "softpipe", "swrast", "software rasterizer" -Quiet
    return (($hasVulkanMarker -or ($hasProbeExitOk -and $hasHardwareVenus)) -and -not $hasSoftwareOnly)
}

function Get-SerialLogVulkanFailureHint {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return "" }
    if (Select-String -LiteralPath $Path -Pattern "Virtio-GPU Venus .*llvmpipe", "llvmpipe", "lavapipe", "rejected software/no-graphics Vulkan" -Quiet) {
        return "Venus llego hasta el ICD virtio, pero el render server del host anuncio llvmpipe/lavapipe. Eso no es Vulkan hardware real."
    }
    if (Select-String -LiteralPath $Path -Pattern "\[ridux-vulkan\] physical-device-count=0", "no physical devices" -Quiet) {
        return "El loader Vulkan cargo, pero no enumero ningun dispositivo fisico usable."
    }
    return ""
}

function Assert-QemuWayfireReadiness {
    param([string]$Serial, [string]$QemuErr = "")

    if ($RequireVirgl -and -not (Test-SerialLogHasVirglGpu $Serial)) {
        throw "QEMU arranco, pero el serial no confirma renderer virgl acelerado. Revisa $Serial."
    }
    if ($RequireVulkan -and -not (Test-SerialLogHasVulkanGpu $Serial)) {
        $hint = Get-SerialLogVulkanFailureHint -Path $Serial
        if ($hint) {
            throw "QEMU arranco, pero Vulkan no es acelerado: $hint Revisa $Serial."
        }
        if ($QemuErr -and (Test-Path $QemuErr) -and
            (Select-String -LiteralPath $QemuErr -Pattern "Render server support was not enabled", "failed to initialize venus renderer", "virgl could not be initialized" -Quiet)) {
            throw ("QEMU arranco, pero Venus fallo en el host: el virglrenderer de QEMU no tiene render-server/Venus usable. " +
                   "No voy a aceptar llvmpipe como Vulkan real. Revisa $QemuErr.")
        }
        throw "QEMU arranco, pero el serial no confirma Vulkan acelerado. Revisa $Serial."
    }
    if ($RequireGpuLadder -and -not (Test-SerialLogHasGpuLadder $Serial)) {
        throw "QEMU arranco, pero ridux-gpu-ladder no llego a overall=ok. Revisa $Serial."
    }
}

function Confirm-QemuScreenshotOrWarn {
    param(
        [string]$Path,
        [string]$Serial,
        [string]$QemuOut,
        [string]$QemuErr
    )
    if ((Test-Path $Path) -and (Get-Item $Path).Length -gt 0) {
        Convert-QemuScreendumpIfNeeded $Path
        return
    }

    if (Test-SerialLogHasVirglGpu $Serial) {
        Write-Warning "No se pudo generar screenshot en '$Path', pero el serial confirma renderer virgl acelerado. En SDL/OpenGL QEMU puede no exponer captura por monitor."
        Write-Warning "Revisa serial: $Serial"
        return
    }

    throw "No se pudo generar screenshot en '$Path'. Revisa $QemuOut y $QemuErr."
}

function ConvertTo-WindowsArg {
    param([string]$Text)
    if ($Text -notmatch '[\s"]') { return $Text }
    return '"' + ($Text -replace '"', '\"') + '"'
}

function Find-HostQemuPath {
    $cmd = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $candidate = Join-Path $env:ProgramFiles "qemu\qemu-system-x86_64.exe"
    if (Test-Path $candidate) { return $candidate }
    return ""
}

function Get-HostQemuAccelerator {
    param([string]$QemuExe)
    if ($env:RIDUX_QEMU_ACCEL) { return $env:RIDUX_QEMU_ACCEL }
    try {
        $help = (& $QemuExe -accel help 2>&1 | Out-String)
        if ($help -match "(?im)^\s*whpx\s*$") { return "whpx" }
    } catch {
        [void]$_.Exception.Message
    }
    return "tcg"
}

function Resolve-HostQemuMachine {
    param([string]$MachineValue, [string]$Accel)
    if ($Accel -ne "whpx") { return $MachineValue }
    if ($MachineValue -match "kernel-irqchip=") { return $MachineValue }
    return "$MachineValue,kernel-irqchip=off"
}

function Test-GpuDeviceRequestsVenus {
    param([string]$Device)
    return ($Device -match "(?i)(^|,)venus=(true|on|yes|1)(,|$)")
}

function Assert-WslHostVulkanHardware {
    param(
        [string]$RepoRoot,
        [string]$PreflightScript,
        [string]$PreflightLog
    )

    $repoWsl = ConvertTo-WslPath $RepoRoot
    $scriptWsl = ConvertTo-WslPath $PreflightScript
    $logWsl = ConvertTo-WslPath $PreflightLog

    $preflight = @"
set -e
repo=$(ConvertTo-BashQuoted $repoWsl)
log=$(ConvertTo-BashQuoted $logWsl)
src="`$repo/tools/ridux_vulkan_probe.c"
probe="/tmp/ridux-host-vulkan-probe"
mkdir -p "`$(dirname "`$log")"
{
  echo "[ridux-host-vulkan] strict hardware preflight"
  echo "kernel=`$(uname -r)"
  if [ -e /dev/dxg ]; then echo "dxg=yes"; else echo "dxg=no"; fi
  if ls /dev/dri/renderD* >/dev/null 2>&1; then echo "dri-render=`$(ls /dev/dri/renderD* | tr '\n' ' ')"; else echo "dri-render=none"; fi
  echo "icd-dir=/usr/share/vulkan/icd.d"
  ls /usr/share/vulkan/icd.d/*.json 2>/dev/null || true
  echo "wslg-icd-dir=/mnt/wslg/distro/usr/share/vulkan/icd.d"
  ls /mnt/wslg/distro/usr/share/vulkan/icd.d/*.json 2>/dev/null || true
} > "`$log" 2>&1

if [ ! -f "`$src" ]; then
  echo "missing probe source: `$src" >> "`$log"
  exit 2
fi
if ! command -v gcc >/dev/null 2>&1; then
  echo "gcc is required for the host Vulkan preflight" >> "`$log"
  exit 2
fi
if ! pkg-config --exists vulkan 2>/dev/null && ! ldconfig -p 2>/dev/null | grep -q libvulkan.so; then
  echo "Vulkan loader/dev files are required for the host Vulkan preflight" >> "`$log"
  exit 2
fi

gcc -O2 -Wall -Wextra "`$src" -lvulkan -o "`$probe" >> "`$log" 2>&1

run_probe() {
  name="`$1"
  shift
  echo "--- `$name ---" >> "`$log"
  set +e
  env "`$@" "`$probe" >> "`$log" 2>&1
  rc="`$?"
  set -e
  echo "rc=`$rc" >> "`$log"
  return "`$rc"
}

common_ld="/usr/lib/wsl/lib:`${LD_LIBRARY_PATH:-}"
if run_probe default LD_LIBRARY_PATH="`$common_ld" LIBGL_ALWAYS_SOFTWARE=0; then
  exit 0
fi

if run_probe no-lvp LD_LIBRARY_PATH="`$common_ld" LIBGL_ALWAYS_SOFTWARE=0 VK_LOADER_DRIVERS_DISABLE="*lvp*"; then
  exit 0
fi

dzn_icd=""
local_dzn="`$repo/build_codex_gpu/host-tools/dozen-prefix/share/vulkan/icd.d/dzn_icd.x86_64.json"
if [ -f "`$local_dzn" ]; then
  dzn_icd="`$local_dzn"
fi
for dir in /usr/share/vulkan/icd.d /mnt/wslg/distro/usr/share/vulkan/icd.d; do
  if [ -n "`$dzn_icd" ]; then
    break
  fi
  candidate="`$(find "`$dir" -maxdepth 1 -type f \( -iname '*dzn*' -o -iname '*d3d12*' \) 2>/dev/null | head -n 1)"
  if [ -n "`$candidate" ]; then
    dzn_icd="`$candidate"
    break
  fi
done

if [ -n "`$dzn_icd" ]; then
  if run_probe d3d12-dozen LD_LIBRARY_PATH="`$repo/build_codex_gpu/host-tools/dozen-prefix/lib/x86_64-linux-gnu:/usr/lib/wsl/lib:/mnt/wslg/distro/usr/lib/x86_64-linux-gnu:`${LD_LIBRARY_PATH:-}" LIBGL_ALWAYS_SOFTWARE=0 VK_ICD_FILENAMES="`$dzn_icd"; then
    exit 0
  fi
else
  echo "no dzn/d3d12 Vulkan ICD found" >> "`$log"
fi

echo "host Vulkan hardware preflight failed; refusing llvmpipe/lavapipe" >> "`$log"
exit 7
"@

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($PreflightScript, (($preflight -replace "`r`n", "`n") + "`n"), $utf8NoBom)
    & wsl bash "$scriptWsl"
    if ($LASTEXITCODE -ne 0) {
        throw ("WSL no expone Vulkan hardware real para Venus. No voy a aceptar llvmpipe/lavapipe como exito. " +
               "Preflight: $PreflightLog")
    }
}

function Assert-HostQemuVenusSupport {
    param(
        [string]$QemuExe,
        [string]$DisplayValue,
        [string]$Device,
        [string]$QemuErr
    )

    if (-not (Test-GpuDeviceRequestsVenus -Device $Device)) { return }

    $dir = Split-Path $QemuErr -Parent
    $preflightErr = Join-Path $dir "qemu-venus-preflight.stderr.log"
    $preflightOut = Join-Path $dir "qemu-venus-preflight.stdout.log"
    Remove-Item -LiteralPath $preflightErr, $preflightOut -Force -ErrorAction SilentlyContinue

    $virglDll = Join-Path (Split-Path $QemuExe -Parent) "libvirglrenderer-1.dll"
    if (Test-Path $virglDll) {
        $dllText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($virglDll))
        if ($dllText.Contains("Render server support was not enabled in virglrenderer") -and
            -not $dllText.Contains("virgl_render_server")) {
            [System.IO.File]::WriteAllText($preflightErr, "libvirglrenderer-1.dll lacks virgl_render_server/Venus render-server support.")
            throw ("QEMU host expone venus=true, pero libvirglrenderer-1.dll no tiene soporte Venus/render-server. " +
                   "No voy a caer a llvmpipe: instala/usa un QEMU+virglrenderer con Venus habilitado, " +
                   "o arranca temporalmente con -GpuDevice `"virtio-vga-gl,blob=true,hostmem=256M,xres=1024,yres=768,id=riduxgpu`" para VirGL/OpenGL. " +
                   "Preflight stderr: $preflightErr")
        }
    }

    $args = @(
        "-machine", "q35,accel=tcg",
        "-nodefaults",
        "-m", "512",
        "-display", $DisplayValue,
        "-device", $Device,
        "-S",
        "-monitor", "none",
        "-serial", "none",
        "-parallel", "none",
        "-name", "RiduxOS_Venus_Preflight"
    )

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $QemuExe
    $psi.Arguments = ($args | ForEach-Object { ConvertTo-WindowsArg $_ }) -join " "
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    $proc = [System.Diagnostics.Process]::Start($psi)
    if (-not $proc.WaitForExit(3000)) {
        try { $proc.Kill() } catch { [void]$_.Exception.Message }
        [void]$proc.WaitForExit(5000)
    }

    $stdout = $proc.StandardOutput.ReadToEnd()
    $stderr = $proc.StandardError.ReadToEnd()
    [System.IO.File]::WriteAllText($preflightOut, $stdout)
    [System.IO.File]::WriteAllText($preflightErr, $stderr)

    if ($stderr -match "(?i)Render server support was not enabled|failed to initialize venus renderer|virgl could not be initialized") {
        throw ("QEMU host expone venus=true, pero su virglrenderer no tiene soporte Venus/render-server. " +
               "No voy a caer a llvmpipe: instala/usa un QEMU+virglrenderer con Venus habilitado, " +
               "o arranca temporalmente con -GpuDevice `"virtio-vga-gl,blob=true,hostmem=256M,xres=1024,yres=768,id=riduxgpu`" para VirGL/OpenGL. " +
               "Preflight stderr: $preflightErr")
    }
}

function Test-QemuMonitorPort {
    param([int]$Port, [int]$TimeoutMs = 500)
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $iar = $client.BeginConnect("127.0.0.1", $Port, $null, $null)
        if (-not $iar.AsyncWaitHandle.WaitOne($TimeoutMs, $false)) {
            $client.Close()
            return $false
        }
        $client.EndConnect($iar)
        $client.Close()
        return $true
    } catch {
        $client.Close()
        return $false
    }
}

function Wait-QemuMonitorPort {
    param([int]$Port, [int]$Seconds = 60)
    for ($i = 0; $i -lt $Seconds; $i++) {
        if (Test-QemuMonitorPort -Port $Port) { return $true }
        Start-Sleep -Seconds 1
    }
    return $false
}

function Send-QemuMonitorCommand {
    param([int]$Port, [string]$Command)
    $client = New-Object System.Net.Sockets.TcpClient
    $client.Connect("127.0.0.1", $Port)
    try {
        $stream = $client.GetStream()
        $bytes = [System.Text.Encoding]::ASCII.GetBytes($Command + "`n")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        Start-Sleep -Milliseconds 150
    } finally {
        $client.Close()
    }
}

function Save-HostQemuOutput {
    param(
        [System.Diagnostics.Process]$Proc,
        [string]$QemuOut,
        [string]$QemuErr
    )
    try {
        if ($Proc -and -not $Proc.HasExited) { [void]$Proc.WaitForExit(1000) }
        if ($Proc) {
            [System.IO.File]::WriteAllText($QemuOut, $Proc.StandardOutput.ReadToEnd())
            [System.IO.File]::WriteAllText($QemuErr, $Proc.StandardError.ReadToEnd())
        }
    } catch {
        Add-Content -LiteralPath $QemuErr -Value $_.Exception.Message
    }
}

function Invoke-HostQemu {
    param(
        [string]$QemuExe,
        [string]$Iso,
        [string]$Serial,
        [string]$Screenshot,
        [string]$QemuOut,
        [string]$QemuErr,
        [string]$PidFile
    )

    if (Test-QemuMonitorPort -Port $MonitorPort) {
        throw "QEMU monitor port $MonitorPort ya esta en uso."
    }

    $displayValue = if ($Display) { $Display } else { "sdl,gl=on" }
    $hostAccel = Get-HostQemuAccelerator -QemuExe $QemuExe
    $machineBase = if ($Machine -match "accel=") { $Machine } else { "$Machine,accel=$hostAccel" }
    $machineValue = Resolve-HostQemuMachine -MachineValue $machineBase -Accel $hostAccel
    Assert-HostQemuVenusSupport -QemuExe $QemuExe -DisplayValue $displayValue -Device $GpuDevice -QemuErr $QemuErr
    $qemuTimeout = [Math]::Max($BootSeconds + 90, 90)
    $args = @(
        "-machine", $machineValue,
        "-cpu", $CpuModel,
        "-m", "$MemoryMB",
        "-smp", "$CpuCount",
        "-boot", "d",
        "-drive", "file=$Iso,media=cdrom,readonly=on,if=ide",
        "-vga", $Vga,
        "-display", $displayValue,
        "-device", $GpuDevice,
        "-serial", "file:$Serial",
        "-monitor", "tcp:127.0.0.1:$MonitorPort,server,nowait",
        "-no-reboot",
        "-name", "RiduxOS_Wayfire_QEMU"
    )

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $QemuExe
    $psi.Arguments = ($args | ForEach-Object { ConvertTo-WindowsArg $_ }) -join " "
    if ($KeepRunning) {
        # In persistent desktop sessions QEMU must outlive this PowerShell
        # wrapper. Do not leave it attached to redirected pipes that are closed
        # when the wrapper exits; the serial log remains the authoritative
        # guest-side diagnostic stream.
        $psi.UseShellExecute = $true
        $psi.RedirectStandardOutput = $false
        $psi.RedirectStandardError = $false
        $psi.CreateNoWindow = $false
    } else {
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.CreateNoWindow = (-not $Interactive)
    }
    $proc = [System.Diagnostics.Process]::Start($psi)
    try {
        if ($ProcessPriority) { $proc.PriorityClass = $ProcessPriority }
    } catch {
        Add-Content -LiteralPath $QemuErr -Value ("priority failed: " + $_.Exception.Message)
    }
    [System.IO.File]::WriteAllText($PidFile, "$($proc.Id)")

    if (-not (Wait-QemuMonitorPort -Port $MonitorPort -Seconds 60)) {
        if (-not $proc.HasExited) { $proc.Kill() }
        if (-not $KeepRunning) {
            Save-HostQemuOutput -Proc $proc -QemuOut $QemuOut -QemuErr $QemuErr
        }
        throw "QEMU host arranco, pero el monitor TCP no abrio en $MonitorPort."
    }

    for ($i = 0; $i -lt $BootSeconds; $i++) {
        if ($proc.HasExited) {
            if (-not $KeepRunning) {
                Save-HostQemuOutput -Proc $proc -QemuOut $QemuOut -QemuErr $QemuErr
            }
            throw "QEMU host salio antes de la captura con codigo $($proc.ExitCode). Revisa $QemuErr."
        }
        Start-Sleep -Seconds 1
    }
    try {
        $fmt = if ($Screenshot.ToLowerInvariant().EndsWith(".png")) { "png" } else { "ppm" }
        Remove-Item -LiteralPath $Screenshot -Force -ErrorAction SilentlyContinue
        Send-QemuMonitorCommand -Port $MonitorPort -Command ("screendump " + $Screenshot + " -f " + $fmt + " riduxgpu 0")
        Start-Sleep -Milliseconds 300
        if (-not (Test-Path $Screenshot) -or (Get-Item $Screenshot).Length -le 0) {
            Send-QemuMonitorCommand -Port $MonitorPort -Command ("screendump " + $Screenshot + " -f " + $fmt)
        }
    } catch {
        Add-Content -LiteralPath $QemuErr -Value ("screendump failed: " + $_.Exception.Message)
        Write-Warning "No pude pedir screendump al monitor QEMU: $($_.Exception.Message)"
    }
    if (-not $KeepRunning) {
        Start-Sleep -Seconds 2
        try {
            Send-QemuMonitorCommand -Port $MonitorPort -Command "quit"
        } catch {
            if (-not $proc.HasExited) { $proc.Kill() }
            Save-HostQemuOutput -Proc $proc -QemuOut $QemuOut -QemuErr $QemuErr
            throw
        }
        if (-not $proc.WaitForExit($qemuTimeout * 1000)) {
            $proc.Kill()
            Save-HostQemuOutput -Proc $proc -QemuOut $QemuOut -QemuErr $QemuErr
            throw "QEMU host no salio despues del timeout."
        }
    }

    if (-not $KeepRunning) {
        Save-HostQemuOutput -Proc $proc -QemuOut $QemuOut -QemuErr $QemuErr
        if ($proc.ExitCode -ne 0) {
            throw "QEMU host salio con codigo $($proc.ExitCode). Revisa $QemuErr."
        }
    }
}

$SerialFullPath = [System.IO.Path]::GetFullPath($SerialLogPath)
$ScreenshotFullPath = [System.IO.Path]::GetFullPath($ScreenshotPath)
$QemuOutFullPath = Join-Path (Split-Path $SerialFullPath -Parent) "qemu-wayfire-safe.stdout.log"
$QemuErrFullPath = Join-Path (Split-Path $SerialFullPath -Parent) "qemu-wayfire-safe.stderr.log"
$PidFileFullPath = Join-Path (Split-Path $SerialFullPath -Parent) "qemu-wayfire-safe.pid"
$LaunchScriptFullPath = Join-Path (Split-Path $SerialFullPath -Parent) "qemu-wayfire-launch.sh"
$MonitorScriptFullPath = Join-Path (Split-Path $SerialFullPath -Parent) "qemu-wayfire-monitor.sh"
$MonitorLogFullPath = Join-Path (Split-Path $SerialFullPath -Parent) "qemu-wayfire-monitor.log"
$HostVulkanPreflightScriptFullPath = Join-Path (Split-Path $SerialFullPath -Parent) "qemu-wayfire-host-vulkan-preflight.sh"
$HostVulkanPreflightLogFullPath = Join-Path (Split-Path $SerialFullPath -Parent) "qemu-wayfire-host-vulkan-preflight.log"
$LocalRenderServerFullPath = Join-Path (Split-Path $SerialFullPath -Parent) "host-tools\virgl-server\usr\libexec\virgl_render_server"

New-Item -ItemType Directory -Path (Split-Path $SerialFullPath -Parent) -Force | Out-Null
Remove-Item -LiteralPath $SerialFullPath, $ScreenshotFullPath, $QemuOutFullPath, $QemuErrFullPath, $PidFileFullPath, $LaunchScriptFullPath, $MonitorScriptFullPath, $MonitorLogFullPath, $HostVulkanPreflightScriptFullPath, $HostVulkanPreflightLogFullPath -Force -ErrorAction SilentlyContinue
New-Item -ItemType File -Path $SerialFullPath -Force | Out-Null

$HostQemuPath = if ($UseHostQemu -and -not $UseWslQemu) { Find-HostQemuPath } else { "" }
if ($HostQemuPath) {
    Invoke-HostQemu -QemuExe $HostQemuPath -Iso $IsoFullPath -Serial $SerialFullPath -Screenshot $ScreenshotFullPath -QemuOut $QemuOutFullPath -QemuErr $QemuErrFullPath -PidFile $PidFileFullPath

    if (-not $KeepRunning) {
        Start-Sleep -Seconds 2
    }

    Confirm-QemuScreenshotOrWarn -Path $ScreenshotFullPath -Serial $SerialFullPath -QemuOut $QemuOutFullPath -QemuErr $QemuErrFullPath
    Assert-QemuWayfireReadiness -Serial $SerialFullPath -QemuErr $QemuErrFullPath

    Write-Output "QEMU host arranco correctamente. Captura: $ScreenshotFullPath"
    Write-Output "Serial log: $SerialFullPath"
    Write-Output "QEMU stdout: $QemuOutFullPath"
    Write-Output "QEMU stderr: $QemuErrFullPath"
    if ($KeepRunning) {
        Write-Output "QEMU sigue en ejecucion porque se paso -KeepRunning."
    }
    return
}

$qemuPath = (& wsl bash -lc "command -v qemu-system-x86_64" 2>$null | Select-Object -First 1).Trim()
if (-not $qemuPath) {
    throw "qemu-system-x86_64 no esta instalado en WSL ni encontre QEMU host."
}

$IsoWsl = ConvertTo-WslPath $IsoFullPath
$SerialWsl = ConvertTo-WslPath $SerialFullPath
$ScreenshotWsl = ConvertTo-WslPath $ScreenshotFullPath
$QemuOutWsl = ConvertTo-WslPath $QemuOutFullPath
$PidFileWsl = ConvertTo-WslPath $PidFileFullPath
$LaunchScriptWsl = ConvertTo-WslPath $LaunchScriptFullPath
$MonitorScriptWsl = ConvertTo-WslPath $MonitorScriptFullPath
$MonitorLogWsl = ConvertTo-WslPath $MonitorLogFullPath
$RepoRootFullPath = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$RepoRootWsl = ConvertTo-WslPath $RepoRootFullPath
$LocalDozenPrefixWsl = "$RepoRootWsl/build_codex_gpu/host-tools/dozen-prefix"
$LocalDozenIcdWsl = "$LocalDozenPrefixWsl/share/vulkan/icd.d/dzn_icd.x86_64.json"
$RenderServerWsl = ""
if (Test-Path $LocalRenderServerFullPath) {
    $RenderServerWsl = ConvertTo-WslPath $LocalRenderServerFullPath
}

if ($RequireVulkan -and (Test-GpuDeviceRequestsVenus -Device $GpuDevice)) {
    Assert-WslHostVulkanHardware -RepoRoot $RepoRootFullPath -PreflightScript $HostVulkanPreflightScriptFullPath -PreflightLog $HostVulkanPreflightLogFullPath
}

$hasRenderNode = (& wsl bash -lc "test -e /dev/dri/renderD128 && echo 1 || echo 0" | Select-Object -First 1).Trim()
$display = if ($Display) { $Display } elseif ($Interactive -or $hasRenderNode -ne "1") { "gtk,gl=on" } else { "egl-headless,gl=on" }
$accel = (& wsl bash -lc "test -e /dev/kvm && echo kvm || echo tcg" | Select-Object -First 1).Trim()
if (-not $accel) { $accel = "tcg" }
$machineArg = if ($Machine -match "accel=") { "-machine $Machine" } else { "-machine $Machine,accel=$accel" }
$qemuTimeout = [Math]::Max($BootSeconds + 90, 90)

$qemuCmd = @(
    (ConvertTo-BashQuoted $qemuPath),
    $machineArg,
    "-cpu $(ConvertTo-BashQuoted $CpuModel)",
    "-m $MemoryMB",
    "-smp $CpuCount",
    "-boot d",
    "-drive file=$(ConvertTo-BashQuoted $IsoWsl),media=cdrom,readonly=on,if=ide",
    "-vga $Vga",
    "-display $display",
    "-device $GpuDevice",
    "-serial file:$(ConvertTo-BashQuoted $SerialWsl)",
    "-monitor tcp:127.0.0.1:$MonitorPort,server,nowait",
    "-no-reboot",
    "-name RiduxOS_Wayfire_QEMU"
) -join " "
$qemuExec = if ($KeepRunning) {
    "$qemuCmd >$(ConvertTo-BashQuoted $QemuOutWsl) 2>&1"
} else {
    "set +e; timeout --foreground -k 5 $qemuTimeout $qemuCmd >$(ConvertTo-BashQuoted $QemuOutWsl) 2>&1; rc=`$?; set -e; if [ `$rc -ne 0 ] && [ `$rc -ne 124 ]; then exit `$rc; fi"
}

$strictVulkan = if ($RequireVulkan) { "1" } else { "0" }
$launch = @"
set -e
export LD_LIBRARY_PATH=/usr/lib/wsl/lib:`${LD_LIBRARY_PATH:-}
if [ -f $(ConvertTo-BashQuoted $LocalDozenIcdWsl) ]; then
  export LD_LIBRARY_PATH=$(ConvertTo-BashQuoted "$LocalDozenPrefixWsl/lib/x86_64-linux-gnu"):`$LD_LIBRARY_PATH
fi
export LIBGL_ALWAYS_SOFTWARE="`${LIBGL_ALWAYS_SOFTWARE:-0}"
if [ -e /dev/dxg ] && ! ls /dev/dri/renderD* >/dev/null 2>&1; then
  export GALLIUM_DRIVER="`${GALLIUM_DRIVER:-d3d12}"
  export MESA_LOADER_DRIVER_OVERRIDE="`${MESA_LOADER_DRIVER_OVERRIDE:-d3d12}"
fi
if [ '$strictVulkan' = '1' ]; then
  if [ -f $(ConvertTo-BashQuoted $LocalDozenIcdWsl) ]; then
    export VK_ICD_FILENAMES=$(ConvertTo-BashQuoted $LocalDozenIcdWsl)
    export VK_DRIVER_FILES=$(ConvertTo-BashQuoted $LocalDozenIcdWsl)
  fi
  export VK_LOADER_DRIVERS_DISABLE="`${VK_LOADER_DRIVERS_DISABLE:-*lvp*}"
fi
if [ -n $(ConvertTo-BashQuoted $RenderServerWsl) ] && [ -x $(ConvertTo-BashQuoted $RenderServerWsl) ]; then
  export RENDER_SERVER_EXEC_PATH=$(ConvertTo-BashQuoted $RenderServerWsl)
fi
if (echo > /dev/tcp/127.0.0.1/$MonitorPort) >/dev/null 2>&1; then
  echo "QEMU monitor port $MonitorPort is already in use" >&2
  exit 2
fi
rm -f $(ConvertTo-BashQuoted $PidFileWsl)
(
  sleep $BootSeconds
  for i in `$(seq 1 30); do
    if (echo > /dev/tcp/127.0.0.1/$MonitorPort) >/dev/null 2>&1; then
      break
    fi
    sleep 1
  done
  python3 - $(ConvertTo-BashQuoted "$MonitorPort") $(ConvertTo-BashQuoted $ScreenshotWsl) $(ConvertTo-BashQuoted $MonitorLogWsl) $(ConvertTo-BashQuoted ($(if ($KeepRunning) { "1" } else { "0" }))) <<'PY' || true
import socket, sys, time
port = int(sys.argv[1])
shot = sys.argv[2]
log_path = sys.argv[3]
keep_running = sys.argv[4] == "1"
def recv_some(sock, seconds=1.5):
    chunks = []
    end = time.time() + seconds
    while time.time() < end:
        try:
            data = sock.recv(4096)
        except socket.timeout:
            break
        if not data:
            break
        chunks.append(data)
        if b"(qemu)" in data:
            break
    return b"".join(chunks).decode("utf-8", "replace")
with open(log_path, "a", encoding="utf-8") as log:
    s = socket.create_connection(("127.0.0.1", port), timeout=8)
    s.settimeout(1)
    log.write(recv_some(s))
    fmt = "png" if shot.lower().endswith(".png") else "ppm"
    commands = [f"screendump {shot} -f {fmt} riduxgpu 0"]
    commands.append(f"screendump {shot} -f {fmt}")
    if not keep_running:
        commands.append("quit")
    for idx, cmd in enumerate(commands):
        log.write(f"\n>>> {cmd}\n")
        s.sendall((cmd + "\n").encode("utf-8"))
        time.sleep(1)
        log.write(recv_some(s))
        if idx == 0:
            try:
                import os
                if os.path.exists(shot) and os.path.getsize(shot) > 0:
                    commands.pop(1)
            except OSError:
                pass
    s.close()
PY
) &
echo `$! > $(ConvertTo-BashQuoted $PidFileWsl)
$qemuExec
"@

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($LaunchScriptFullPath, (($launch -replace "`r`n", "`n") + "`n"), $utf8NoBom)
& wsl bash "$LaunchScriptWsl"
if ($LASTEXITCODE -ne 0) {
    throw "No se pudo arrancar QEMU. Revisa $QemuOutFullPath."
}

if (-not $KeepRunning) {
    Start-Sleep -Seconds 2
}

Confirm-QemuScreenshotOrWarn -Path $ScreenshotFullPath -Serial $SerialFullPath -QemuOut $QemuOutFullPath -QemuErr $QemuOutFullPath
Assert-QemuWayfireReadiness -Serial $SerialFullPath -QemuErr $QemuOutFullPath

Write-Output "QEMU arranco correctamente. Captura: $ScreenshotFullPath"
Write-Output "Serial log: $SerialFullPath"
Write-Output "QEMU stdout/stderr: $QemuOutFullPath"
if ($KeepRunning) {
    Write-Output "QEMU sigue en ejecucion porque se paso -KeepRunning."
}
