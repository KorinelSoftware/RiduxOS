param(
    [string]$IsoPath = ".\build_codex_gpu\RiduxOS-Unix.iso",
    [string]$BuildDir = ".\build_codex_gpu",
    [int]$BootSeconds = 60,
    [int]$MemoryMB = 4096,
    [int]$CpuCount = 4,
    [int]$MonitorPort = 45454,
    [ValidateSet("Auto", "Virgl", "Venus")]
    [string]$GraphicsMode = "Auto",
    [string]$Display = "",
    [switch]$ExitAfterProbe
)

$ErrorActionPreference = "Stop"

function Test-HostQemuHasVenusRenderServer {
    $cmd = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
    $qemuExe = if ($cmd) { $cmd.Source } else { Join-Path $env:ProgramFiles "qemu\qemu-system-x86_64.exe" }
    if (-not (Test-Path $qemuExe)) { return $false }

    $virglDll = Join-Path (Split-Path $qemuExe -Parent) "libvirglrenderer-1.dll"
    if (-not (Test-Path $virglDll)) { return $false }

    $dllText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($virglDll))
    return ($dllText.Contains("virgl_render_server") -and
            -not $dllText.Contains("Render server support was not enabled in virglrenderer"))
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$effectiveGraphicsMode = $GraphicsMode
if ($effectiveGraphicsMode -eq "Auto") {
    $effectiveGraphicsMode = if (Test-HostQemuHasVenusRenderServer) { "Venus" } else { "Virgl" }
}

$serial = Join-Path $BuildDir ("qemu-ridux-native-speed-{0}.log" -f $effectiveGraphicsMode.ToLowerInvariant())
$shot = Join-Path $BuildDir ("qemu-ridux-native-speed-{0}.png" -f $effectiveGraphicsMode.ToLowerInvariant())
$gpuDevice = if ($effectiveGraphicsMode -eq "Venus") {
    "virtio-vga-gl,blob=true,venus=true,hostmem=512M,xres=1024,yres=768,id=riduxgpu"
} else {
    "virtio-vga-gl,blob=true,hostmem=512M,xres=1024,yres=768,id=riduxgpu"
}

$env:RIDUX_QEMU_ACCEL = "whpx"

$bootParams = @{
    IsoPath = $IsoPath
    SerialLogPath = $serial
    ScreenshotPath = $shot
    BootSeconds = $BootSeconds
    MemoryMB = $MemoryMB
    CpuCount = $CpuCount
    CpuModel = "qemu64"
    Machine = "q35"
    GpuDevice = $gpuDevice
    UseHostQemu = $true
    RequireVirgl = $true
    MonitorPort = $MonitorPort
    ProcessPriority = "High"
}

if ($Display) {
    $bootParams.Display = $Display
}

if ($effectiveGraphicsMode -eq "Venus") {
    $bootParams.RequireVulkan = $true
}

if (-not $ExitAfterProbe) {
    $bootParams.KeepRunning = $true
}

Write-Host ("Ridux native-speed mode: {0} via WHPX" -f $effectiveGraphicsMode)
& (Join-Path $PSScriptRoot "boot-qemu-wayfire.ps1") @bootParams
