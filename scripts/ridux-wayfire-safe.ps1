param(
    [string]$BuildDir = "build_wayfire_safe",
    [string]$BaseIso = "",
    # Single canonical ISO at the repo root that gets rewritten in place on
    # every build, regardless of which BuildDir the kernel/initrd intermediates
    # live in. This stops the build_* directories from each accumulating their
    # own copy of a 200+ MB ISO. Override -OutputIso explicitly only when you
    # really want a parallel ISO.
    [string]$OutputIso = ".\RiduxOS.iso",
    [switch]$Boot,
    [switch]$Qemu,
    [switch]$UseHostQemu,
    [int]$TimeoutSeconds = 900,
    [int]$MaxInitrdMB = 1050,
    [int]$MaxOverlayMB = 256,
    [int]$MaxIsoMB = 1200,
    [int]$MemoryMB = 2048,
    [int]$CpuCount = 2,
    [int]$CpuExecutionCap = 100,
    [int]$BootSeconds = 35,
    [switch]$EnableGpuRenderer,
    [switch]$DisableGpuRenderer,
    [switch]$EnableSoftwareGlesRenderer,
    [switch]$BareMetal,
    [switch]$FetchDesktopStack,
    [switch]$Interactive,
    [switch]$KeepRunning,
    [switch]$EnableFullDesktopStack,
    [switch]$DisableFullDesktopStack,
    [switch]$DisableScanoutMirror,
    [switch]$EnableScanoutProbe,
    [switch]$DisableShaderCache
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot
$EffectiveSoftwareGlesRenderer = $EnableSoftwareGlesRenderer -or $BareMetal
if ($EnableGpuRenderer -and $DisableGpuRenderer) {
    throw "Usa solo uno: -EnableGpuRenderer o -DisableGpuRenderer."
}
# In QEMU this route is intentionally GPU-first: Wayfire must come up through
# wlroots EGL/GLES on Mesa/VirGL, not through the old pixman/present2d baseline.
$EffectiveGpuRenderer = ($EnableGpuRenderer -or $Qemu) -and -not $DisableGpuRenderer -and -not $EffectiveSoftwareGlesRenderer
$GpuRendererFlag = if ($EffectiveGpuRenderer -or $EffectiveSoftwareGlesRenderer) { "1" } else { "0" }
$VBoxGpuFlag = if ($EffectiveGpuRenderer -and -not $Qemu) { "1" } else { "0" }
$SoftwareGlesFlag = if ($EffectiveSoftwareGlesRenderer) { "1" } else { "0" }
$PixmanRendererFlag = if (-not $EffectiveGpuRenderer -and -not $EffectiveSoftwareGlesRenderer) { "1" } else { "0" }
$FetchDesktopStackFlag = if ($FetchDesktopStack) { "1" } else { "0" }
$EffectiveFullDesktopStack = (-not $DisableFullDesktopStack) -or $EnableFullDesktopStack
$FullDesktopStackFlag = if ($EffectiveFullDesktopStack) { "1" } else { "0" }
$ScanoutMirrorDisableFlag = if ($DisableScanoutMirror -or $Qemu) { "1" } else { "0" }
$ScanoutProbeFlag = if ($EnableScanoutProbe) { "1" } else { "0" }
$DisableShaderCacheFlag = if ($DisableShaderCache) { "1" } else { "0" }
$Present2DFlag = "0"
$WayfirePrimaryFlag = "1"
$WayfireStrictStackFlag = "1"
$WayfireVirtioGpuFlag = if ($Qemu -and $EffectiveGpuRenderer) { "1" } else { "0" }
if ($Qemu -and -not $PSBoundParameters.ContainsKey("BootSeconds")) {
    $BootSeconds = 180
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

function Assert-InRepo {
    param([string]$Path)
    $full = [System.IO.Path]::GetFullPath($Path)
    $root = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd("\") + "\"
    if (-not $full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Por seguridad no voy a escribir fuera del repo: $full"
    }
}

function Invoke-WslSafe {
    param(
        [string]$Command,
        [int]$Timeout = 0
    )

    $repoWsl = ConvertTo-WslPath $RepoRoot
    $tmpScript = [System.IO.Path]::GetTempFileName() + ".sh"
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText(
        $tmpScript,
        "#!/usr/bin/env bash`nset -e`ncd $(ConvertTo-BashQuoted $repoWsl)`n$Command`n",
        $utf8NoBom
    )

    $scriptWsl = ConvertTo-WslPath $tmpScript
    $runner = "bash $(ConvertTo-BashQuoted $scriptWsl)"
    if ($Timeout -gt 0) {
        $runner = "timeout --kill-after=15s ${Timeout}s bash $(ConvertTo-BashQuoted $scriptWsl)"
    }

    try {
        & wsl bash -lc "$runner"
        if ($LASTEXITCODE -ne 0) {
            throw "Fallo comando WSL seguro: $Command"
        }
    } finally {
        Remove-Item -LiteralPath $tmpScript -Force -ErrorAction SilentlyContinue
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

function Assert-MaxMB {
    param(
        [string]$Path,
        [int]$MaxMB,
        [string]$Name
    )

    $item = Get-Item $Path
    $mb = [math]::Round($item.Length / 1MB, 1)
    if ($mb -gt $MaxMB) {
        throw "$Name demasiado grande para este modo seguro: $mb MB (limite: $MaxMB MB)."
    }
    return $mb
}

function Stop-RiduxVirtualBoxVm {
    param([string]$VmName = "RiduxOS_Wayfire_Safe")
    $vbox = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
    if (-not (Test-Path $vbox)) { return }

    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $running = & $vbox list runningvms 2>$null
    if ($LASTEXITCODE -eq 0 -and ($running -match "`"$VmName`"")) {
        Write-Host "VM Wayfire abierta detectada. Apagando para liberar recursos..."
        & $vbox controlvm $VmName poweroff 2>$null | Out-Null
        Start-Sleep -Seconds 3
    }
    $ErrorActionPreference = $oldEap
}

Assert-InRepo $BuildDir
Assert-InRepo $OutputIso

$BuildDirFull = [System.IO.Path]::GetFullPath($BuildDir)
$OutputIsoFull = [System.IO.Path]::GetFullPath($OutputIso)

# Si la VM sigue viva o tiene montada la ISO anterior, Windows bloquea el archivo
# y xorriso no puede reemplazarlo. La apagamos antes de leer/recrear la imagen.
Stop-RiduxVirtualBoxVm

if ([string]::IsNullOrWhiteSpace($BaseIso)) {
    # Prefer the canonical ISO at the repo root, then any historical build_*
    # ISO already present. The first match wins so that successive runs reuse
    # the same booteable template even when called with different BuildDirs.
    $candidates = @(
        $OutputIsoFull,
        ".\RiduxOS.iso",
        ".\build_wayfire_safe\RiduxOS-Wayfire-Full-Desktop.iso",
        ".\build_codex_check\RiduxOS-Unix-firefox-exec-repair-20260505.iso"
    )
    foreach ($cand in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($cand) -and (Test-Path $cand)) {
            $BaseIso = $cand
            break
        }
    }
    if ([string]::IsNullOrWhiteSpace($BaseIso)) {
        # Last resort: pick the first booteable build_*\*.iso the workspace has.
        $fallback = Get-ChildItem -Path ".\" -Recurse -Filter "*.iso" -ErrorAction SilentlyContinue |
            Where-Object { $_.Length -gt 100MB } |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if ($fallback) { $BaseIso = $fallback.FullName }
    }
    if ([string]::IsNullOrWhiteSpace($BaseIso)) {
        throw "No hay ISO plantilla. Genera una vez $OutputIso o pasa -BaseIso con una ISO booteable."
    }
}
Assert-InRepo $BaseIso
Assert-MinSize $BaseIso 100MB "ISO base"

$BuildDirWsl = ConvertTo-WslPath $BuildDirFull
$BaseIsoWsl = ConvertTo-WslPath $BaseIso
$OutputIsoWsl = ConvertTo-WslPath $OutputIsoFull

New-Item -ItemType Directory -Force -Path $BuildDirFull | Out-Null

Write-Host "== Wayfire safe build =="
Write-Host "Build dir: $BuildDirFull"
if ($BareMetal) { Write-Host "Modo bare-metal: framebuffer/GLES2 software, sin marcador VirtualBox GPU." }
if ($Qemu -and $EffectiveGpuRenderer) { Write-Host "Modo QEMU GPU: Mesa usara virtio_gpu/virgl en lugar de vmwgfx/VMSVGA." }
if ($Qemu -and $PixmanRendererFlag -eq "1") { throw "QEMU Wayfire estricto requiere GPU/VirGL; no se permite pixman en esta ruta." }

Write-Host "== Wayfire shell assets =="
$wayfireShellCmd = @"
mkdir -p rootfs/opt/wayfire/bin rootfs/opt/wayfire/share/wayfire rootfs/usr/bin
mkdir -p rootfs/etc
if [ "$WayfirePrimaryFlag" = "1" ]; then
  : > rootfs/etc/ridux-wayfire-primary.enable
fi
if [ "$WayfireStrictStackFlag" = "1" ]; then
  : > rootfs/etc/ridux-wayfire-strict-stack.enable
fi
if [ "$WayfireVirtioGpuFlag" = "1" ]; then
  : > rootfs/etc/ridux-wayfire-virtio-gpu.enable
  : > rootfs/etc/ridux-display-refresh-144.enable
  rm -f rootfs/etc/ridux-display-refresh-60.enable
else
  rm -f rootfs/etc/ridux-wayfire-virtio-gpu.enable
  rm -f rootfs/etc/ridux-display-refresh-144.enable
fi
if [ "$DisableShaderCacheFlag" = "1" ]; then
  : > rootfs/etc/ridux-wayfire-disable-shader-cache.enable
else
  rm -f rootfs/etc/ridux-wayfire-disable-shader-cache.enable
fi
if [ "$GpuRendererFlag" = "1" ]; then
  mkdir -p rootfs/etc
  : > rootfs/etc/ridux-wayfire-gpu.enable
else
  rm -f rootfs/etc/ridux-wayfire-gpu.enable
fi
if [ "$VBoxGpuFlag" = "1" ]; then
  mkdir -p rootfs/etc
  : > rootfs/etc/ridux-wayfire-vbox-gpu.enable
else
  rm -f rootfs/etc/ridux-wayfire-vbox-gpu.enable
fi
if [ "$SoftwareGlesFlag" = "1" ]; then
  mkdir -p rootfs/etc
  : > rootfs/etc/ridux-wayfire-gles2-software.enable
else
  rm -f rootfs/etc/ridux-wayfire-gles2-software.enable
fi
if [ "$PixmanRendererFlag" = "1" ]; then
  mkdir -p rootfs/etc
  : > rootfs/etc/ridux-wayfire-pixman.enable
else
  rm -f rootfs/etc/ridux-wayfire-pixman.enable
fi
if [ "$FullDesktopStackFlag" = "1" ]; then
  mkdir -p rootfs/etc
  : > rootfs/etc/ridux-wayfire-full-stack.enable
else
  rm -f rootfs/etc/ridux-wayfire-full-stack.enable
fi
if [ "$ScanoutMirrorDisableFlag" = "1" ]; then
  mkdir -p rootfs/etc
  : > rootfs/etc/ridux-wayfire-scanout-mirror.disable
else
  rm -f rootfs/etc/ridux-wayfire-scanout-mirror.disable
fi
if [ "$ScanoutProbeFlag" = "1" ]; then
  mkdir -p rootfs/etc
  : > rootfs/etc/ridux-wayfire-scanout-probe.enable
else
  rm -f rootfs/etc/ridux-wayfire-scanout-probe.enable
fi
if [ "$Present2DFlag" = "1" ]; then
  mkdir -p rootfs/etc
  : > rootfs/etc/ridux-wayfire-present2d.enable
else
  rm -f rootfs/etc/ridux-wayfire-present2d.enable
fi
if [ -d third_party/wayfire/install/bin ]; then
  for n in wayfire wayland-logout wcm wf-background wf-panel wf-dock; do
    [ ! -x third_party/wayfire/install/bin/`$n ] || cp third_party/wayfire/install/bin/`$n rootfs/opt/wayfire/bin/`$n
  done
fi
if [ -d third_party/wayfire/install/share ]; then
  mkdir -p rootfs/opt/wayfire/share
  cp -a third_party/wayfire/install/share/. rootfs/opt/wayfire/share/
fi
if [ -d third_party/wayfire/install/share/wayfire/metadata ]; then
  baked_prefix="/mnt/c/Users/SEBA/Downloads/RiduxOS/third_party/wayfire/install"
  if [ -f rootfs/opt/wayfire/SOURCE-MANIFEST.txt ]; then
    baked_prefix="`$(sed -n 's/^source_prefix=//p' rootfs/opt/wayfire/SOURCE-MANIFEST.txt | head -n1)"
  fi
  if [ -n "`$baked_prefix" ] && [ "`$baked_prefix" != "/" ]; then
    mkdir -p "rootfs`$baked_prefix/share/wayfire"
    cp -a third_party/wayfire/install/share/wayfire/metadata "rootfs`$baked_prefix/share/wayfire/"
    if [ -d third_party/wayfire/install/lib/wayfire ]; then
      mkdir -p "rootfs`$baked_prefix/lib"
      cp -a third_party/wayfire/install/lib/wayfire "rootfs`$baked_prefix/lib/"
    fi
  fi
fi
if ! pkg-config --exists gtk+-3.0 gtk-layer-shell-0; then
  echo "Falta gtk+-3.0 o gtk-layer-shell-0 para compilar el shell Wayland." >&2
  exit 1
fi
if ! pkg-config --exists wayland-client; then
  echo "Falta wayland-client para compilar el shell Wayland visible." >&2
  exit 1
fi
if ! command -v wayland-scanner >/dev/null 2>&1; then
  echo "Falta wayland-scanner para generar xdg-shell." >&2
  exit 1
fi
if ! command -v convert >/dev/null 2>&1; then
  echo "Falta ImageMagick convert para generar el wallpaper PPM simple." >&2
  exit 1
fi
mkdir -p rootfs/tmp/wayfire-build
wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml rootfs/tmp/wayfire-build/xdg-shell-client-protocol.h
wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml rootfs/tmp/wayfire-build/xdg-shell-protocol.c
gcc tools/wayfire/ridux_wayland_shell.c -O2 -Wall -Wextra \
  -o rootfs/opt/wayfire/bin/ridux-launcher \
  -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
  -Wl,-rpath,/lib/x86_64-linux-gnu \
  -Wl,-rpath,/lib64 \
  -Wl,-rpath,/usr/lib \
  `$(pkg-config --cflags --libs gtk+-3.0 gtk-layer-shell-0)
gcc tools/wayfire/ridux_panel_wayland.c rootfs/tmp/wayfire-build/xdg-shell-protocol.c -O2 -Wall -Wextra \
  -Irootfs/tmp/wayfire-build \
  -o rootfs/opt/wayfire/bin/ridux-panel \
  -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
  -Wl,-rpath,/lib/x86_64-linux-gnu \
  -Wl,-rpath,/lib64 \
  -Wl,-rpath,/usr/lib \
  `$(pkg-config --cflags --libs wayland-client)
gcc tools/wayfire/ridux_visible_shell.c rootfs/tmp/wayfire-build/xdg-shell-protocol.c -O2 -Wall -Wextra \
  -Irootfs/tmp/wayfire-build \
  -o rootfs/opt/wayfire/bin/ridux-visible-shell \
  -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
  -Wl,-rpath,/lib/x86_64-linux-gnu \
  -Wl,-rpath,/lib64 \
  -Wl,-rpath,/usr/lib \
  `$(pkg-config --cflags --libs wayland-client)
gcc tools/wayfire/ridux_gpu_ladder.c rootfs/tmp/wayfire-build/xdg-shell-protocol.c -O2 -Wall -Wextra \
  -Irootfs/tmp/wayfire-build \
  -o rootfs/opt/wayfire/bin/ridux-gpu-ladder \
  -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
  -Wl,-rpath,/lib/x86_64-linux-gnu \
  -Wl,-rpath,/lib64 \
  -Wl,-rpath,/usr/lib \
  `$(pkg-config --cflags --libs wayland-client)
gcc tools/wayfire/ridux_wayland_session.c -O2 -Wall -Wextra \
  -o rootfs/opt/wayfire/bin/ridux-session \
  -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
  -Wl,-rpath,/lib/x86_64-linux-gnu \
  -Wl,-rpath,/lib64 \
  -Wl,-rpath,/usr/lib
mkdir -p rootfs/opt/wayfire/lib
gcc tools/wayfire/ridux_udev_monitor_shim.c -O2 -Wall -Wextra -fPIC -shared \
  -o rootfs/opt/wayfire/lib/ridux-udev-monitor-shim.so \
  -Wl,--version-script=tools/wayfire/ridux_udev_monitor_shim.map \
  -ldl
gcc tools/wayfire/ridux_qt_freeguard.c -O2 -Wall -Wextra -fPIC -shared \
  -o rootfs/opt/wayfire/lib/ridux-client-freeguard.so \
  -ldl -pthread
rm -f rootfs/opt/wayfire/lib/ridux-qt-freeguard.so
rm -f rootfs/opt/wayfire/bin/ridux-qt-dashboard \
      rootfs/opt/wayfire/bin/ridux-qt-files \
      rootfs/opt/wayfire/bin/ridux-qt-monitor
if [ "`${RIDUX_BUILD_QT_SHOWCASE:-0}" = "1" ]; then
  if ! pkg-config --exists Qt6Widgets; then
    echo "Falta Qt6Widgets para compilar las apps Qt de Ridux." >&2
    exit 1
  fi
  g++ tools/wayfire/ridux_qt_suite.cpp -O2 -Wall -Wextra \
    -o rootfs/opt/wayfire/bin/ridux-qt-dashboard \
    -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
    -Wl,-rpath,/lib/x86_64-linux-gnu \
    -Wl,-rpath,/lib64 \
    -Wl,-rpath,/usr/lib \
    `$(pkg-config --cflags --libs Qt6Widgets)
  cp rootfs/opt/wayfire/bin/ridux-qt-dashboard rootfs/opt/wayfire/bin/ridux-qt-files
  cp rootfs/opt/wayfire/bin/ridux-qt-dashboard rootfs/opt/wayfire/bin/ridux-qt-monitor
fi
if [ -f rootfs/opt/wayfire/share/wayfire/wallpaper.jpg ]; then
  cp rootfs/opt/wayfire/share/wayfire/wallpaper.jpg rootfs/opt/wayfire/share/wayfire/ridux-wallpaper.jpg
  convert rootfs/opt/wayfire/share/wayfire/wallpaper.jpg -resize 1024x768^ -gravity center -extent 1024x768 -strip \
    rootfs/opt/wayfire/share/wayfire/ridux-wallpaper.png
else
  convert -size 1024x768 gradient:'#16181c-#283040' \
    rootfs/opt/wayfire/share/wayfire/ridux-wallpaper.png
fi
convert rootfs/opt/wayfire/share/wayfire/ridux-wallpaper.png -strip -compress none PPM:rootfs/opt/wayfire/share/wayfire/ridux-wallpaper.ppm
if command -v glib-compile-schemas >/dev/null 2>&1 && [ -d rootfs/usr/share/glib-2.0/schemas ]; then
  glib-compile-schemas rootfs/usr/share/glib-2.0/schemas || true
fi
mkdir -p rootfs/opt/wayfire/share/wayfire/icons \
  rootfs/opt/wayfire/share/applications \
  rootfs/usr/share/applications \
  rootfs/tmp/fontconfig-cache \
  rootfs/tmp/wayfire-home/cache/fontconfig \
  rootfs/tmp/wayfire-home/config/wf-shell/css \
  rootfs/etc/wayfire/wf-shell/css
cp RiduxIcons/RiduxIconLogo.png rootfs/opt/wayfire/share/wayfire/icons/ridux-logo.png
cp RiduxIcons/FileBrowserIcon.png rootfs/opt/wayfire/share/wayfire/icons/ridux-files.png
cp RiduxIcons/SettingsIcon.png rootfs/opt/wayfire/share/wayfire/icons/ridux-settings.png
cp RiduxIcons/TerminalIcon.png rootfs/opt/wayfire/share/wayfire/icons/ridux-terminal.png
cp RiduxIcons/InfoIcon.png rootfs/opt/wayfire/share/wayfire/icons/ridux-about.png
cp RiduxIcons/AdministradorTareasIcon.png rootfs/opt/wayfire/share/wayfire/icons/ridux-monitor.png
cp RiduxIcons/CalculatorIcon.png rootfs/opt/wayfire/share/wayfire/icons/ridux-calculator.png
for n in ridux-background ridux-dock ridux-files ridux-settings ridux-about ridux-monitor ridux-terminal; do
  cp rootfs/opt/wayfire/bin/ridux-launcher rootfs/opt/wayfire/bin/`$n
done
cp rootfs/opt/wayfire/bin/ridux-launcher rootfs/usr/bin/ridux-launcher
cat > rootfs/tmp/wayfire-home/config/wf-shell/css/default.css <<'EOF'
* {
  font-family: "DejaVu Sans", "Liberation Sans", "Cantarell", sans-serif;
  font-size: 11px;
  color: #eef6ff;
  text-shadow: none;
}

.wf-panel {
  background: transparent;
}

.wf-panel button,
.wf-panel .launcher,
.wf-panel .tray-button,
.wf-panel .window-button,
.wf-panel .clock {
  min-height: 28px;
  margin: 3px 2px;
  padding: 0 9px;
  border-radius: 11px;
  border: 1px solid transparent;
  background: transparent;
  box-shadow: none;
  transition: 120ms ease-out;
}

.wf-panel button:hover,
.wf-panel .launcher:hover,
.wf-panel .window-button:hover,
.wf-panel .clock:hover {
  background: rgba(255, 255, 255, 0.11);
  border-color: rgba(255, 255, 255, 0.15);
}

.wf-panel .launcher image,
.wf-panel .tray-button image {
  transition: 120ms ease-out;
  -gtk-icon-transform: none;
}

.wf-panel .launcher:hover image,
.wf-panel .tray-button:hover image {
  -gtk-icon-transform: scale(1.06);
}

.wf-panel .clock label {
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.02em;
  padding: 0 4px;
}

popover,
popover.background,
.clock-popover,
calendar,
.app-list-scroll,
.categtory-list-scroll {
  background: rgba(13, 20, 32, 0.94);
  border: 1px solid rgba(255, 255, 255, 0.13);
  border-radius: 18px;
  box-shadow: 0 24px 58px rgba(0, 0, 0, 0.36);
}

entry.app-search,
entry {
  min-height: 34px;
  padding: 6px 12px;
  color: #eef6ff;
  caret-color: #62d9ff;
  background: rgba(255, 255, 255, 0.10);
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: 13px;
}

.app-button,
.app-category {
  margin: 5px;
  padding: 10px;
  border-radius: 15px;
  background: rgba(255, 255, 255, 0.07);
  border: 1px solid rgba(255, 255, 255, 0.08);
  transition: 120ms ease-out;
}

.app-button:hover,
.app-category:hover {
  background: rgba(25, 132, 255, 0.23);
  border-color: rgba(112, 211, 255, 0.36);
}

window {
  box-shadow: none;
}

button,
button.flat {
  min-width: 56px;
  min-height: 56px;
  margin: 6px;
  padding: 8px;
  border-radius: 18px;
  border: 1px solid rgba(255, 255, 255, 0.18);
  background: rgba(8, 14, 26, 0.78);
  box-shadow: 0 16px 36px rgba(0, 0, 0, 0.30);
  transition: 120ms ease-out;
}

button:hover,
button.flat:hover,
button:checked {
  background: rgba(25, 132, 255, 0.32);
  border-color: rgba(112, 211, 255, 0.42);
}

button image,
button.flat image {
  -gtk-icon-transform: none;
}
EOF
cp rootfs/tmp/wayfire-home/config/wf-shell/css/default.css rootfs/tmp/wayfire-home/config/wf-shell/css/ridux-shell.css
cp rootfs/tmp/wayfire-home/config/wf-shell/css/default.css rootfs/etc/wayfire/wf-shell/css/default.css
cp rootfs/tmp/wayfire-home/config/wf-shell/css/ridux-shell.css rootfs/etc/wayfire/wf-shell/css/ridux-shell.css
cat > rootfs/tmp/wayfire-home/config/wayfire.ini <<'EOF'
[core]
plugins = alpha animate autostart blur command cube decoration expo foreign-toplevel grid gtk-shell move place resize scale switcher vswitch wayfire-shell window-rules wm-actions wobbly wsets xdg-activation
xwayland = false
preferred_decoration_mode = server
background_color = 0.030 0.036 0.048 1.0

[autostart]
autostart_wf_shell = false
session = /opt/wayfire/bin/ridux-session

[output:*]
mode = auto
position = 0,0
transform = normal
scale = 1.000000

[input]
xkb_layout = us
cursor_theme = Adwaita
cursor_size = 28
mouse_cursor_speed = 0.000000
touchpad_cursor_speed = 0.000000

[workarounds]
enable_input_method_v2 = false

[animate]
open_animation = zoom
close_animation = zoom
duration = 220
startup_duration = 260
enabled_for = all

[blur]
method = kawase
kawase_iterations = 4
kawase_offset = 4
toggle = none

[alpha]
min_value = 0.72
modifier = <alt> <super>

[decoration]
border_size = 1
title_height = 30
active_color = \#172033d8
inactive_color = \#101722a8
font = Cantarell

[scale]
toggle = <super> KEY_TAB
duration = 190
spacing = 24

[expo]
toggle = <super> KEY_E
duration = 220

[cube]
activate = <super> <ctrl> BTN_LEFT
background_mode = skydome
initial_animation = 350

[wobbly]
friction = 5.5
spring_k = 7.5
grid_resolution = 8

[grid]
slot_l = <super> KEY_LEFT
slot_r = <super> KEY_RIGHT
slot_t = <super> KEY_UP
slot_b = <super> KEY_DOWN

[vswitch]
binding_left = <ctrl> <super> KEY_LEFT
binding_right = <ctrl> <super> KEY_RIGHT
binding_up = <ctrl> <super> KEY_UP
binding_down = <ctrl> <super> KEY_DOWN
duration = 180

[command]
binding_terminal = <super> KEY_ENTER
command_terminal = /usr/bin/ridux-terminal
repeatable_binding_terminal = none
always_binding_terminal = none
release_binding_terminal = none
binding_launcher = <super> KEY_SPACE
command_launcher = /usr/bin/ridux-open-launcher
repeatable_binding_launcher = none
always_binding_launcher = none
release_binding_launcher = none
binding_lock = <super> KEY_ESC
command_lock = /usr/bin/ridux-lock
repeatable_binding_lock = none
always_binding_lock = none
release_binding_lock = none
binding_screenshot = KEY_PRINT
command_screenshot = /usr/bin/ridux-screenshot
repeatable_binding_screenshot = none
always_binding_screenshot = none
release_binding_screenshot = none
binding_screenshot_interactive = <shift> KEY_PRINT
command_screenshot_interactive = /usr/bin/ridux-screenshot
repeatable_binding_screenshot_interactive = none
always_binding_screenshot_interactive = none
release_binding_screenshot_interactive = none
EOF
cp rootfs/tmp/wayfire-home/config/wayfire.ini rootfs/etc/wayfire/wayfire.ini
cat > rootfs/tmp/wayfire-home/config/wf-shell.ini <<'EOF'
[panel]
position = top
autohide = false
minimal_height = 36
layer = top
background_color = 0.018 0.024 0.036 0.86
widgets_left = menu spacing8 launchers window-list
widgets_center = none
widgets_right = clock
launcher_cmd_1 = /usr/bin/ridux-open-files
launcher_icon_1 = /opt/wayfire/share/wayfire/icons/ridux-files.png
launcher_label_1 = Files
launcher_cmd_2 = /usr/bin/ridux-terminal
launcher_icon_2 = /opt/wayfire/share/wayfire/icons/ridux-terminal.png
launcher_label_2 = Terminal
launcher_cmd_3 = /usr/bin/ridux-display-settings
launcher_icon_3 = /opt/wayfire/share/wayfire/icons/ridux-monitor.png
launcher_label_3 = Displays
launcher_cmd_4 = /usr/bin/ridux-open-launcher
launcher_icon_4 = /opt/wayfire/share/wayfire/icons/ridux-logo.png
launcher_label_4 = Apps
launchers_size = 24
launchers_spacing = 6
clock_format = %H:%M
menu_icon = /opt/wayfire/share/wayfire/icons/ridux-logo.png
menu_show_categories = false
menu_list = false
menu_min_content_width = 560
menu_min_content_height = 460

[dock]
position = bottom
autohide = false
dock_height = 72
icon_height = 48
icon_mapping_org.xfce.thunar = /opt/wayfire/share/wayfire/icons/ridux-files.png
icon_mapping_thunar = /opt/wayfire/share/wayfire/icons/ridux-files.png
icon_mapping_ridux-terminal = /opt/wayfire/share/wayfire/icons/ridux-terminal.png
icon_mapping_ridux-settings = /opt/wayfire/share/wayfire/icons/ridux-settings.png
icon_mapping_ridux-launcher = /opt/wayfire/share/wayfire/icons/ridux-logo.png
launcher_cmd_1 = /usr/bin/ridux-open-launcher
launcher_icon_1 = /opt/wayfire/share/wayfire/icons/ridux-logo.png
launcher_cmd_2 = /usr/bin/ridux-open-files
launcher_icon_2 = /opt/wayfire/share/wayfire/icons/ridux-files.png
launcher_cmd_3 = /usr/bin/ridux-terminal
launcher_icon_3 = /opt/wayfire/share/wayfire/icons/ridux-terminal.png
launcher_cmd_4 = /usr/bin/ridux-display-settings
launcher_icon_4 = /opt/wayfire/share/wayfire/icons/ridux-monitor.png
launcher_cmd_5 = /usr/bin/ridux-screenshot
launcher_icon_5 = /opt/wayfire/share/wayfire/icons/ridux-monitor.png

[background]
cycle_timeout = 0
image = /opt/wayfire/share/wayfire/ridux-wallpaper.png
fill_mode = fill_and_crop
randomize = 0

[launcher]
terminal = /usr/bin/ridux-terminal
EOF
cp rootfs/tmp/wayfire-home/config/wf-shell.ini rootfs/etc/wayfire/wf-shell.ini
cat > rootfs/opt/wayfire/share/applications/ridux-launcher.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Ridux Launcher
Comment=Open Ridux applications
Exec=/usr/bin/ridux-open-launcher
Icon=/opt/wayfire/share/wayfire/icons/ridux-logo.png
Categories=Utility;System;
EOF
cat > rootfs/opt/wayfire/share/applications/ridux-files.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Files
Comment=Browse files with Thunar
Exec=/usr/bin/ridux-open-files
Icon=/opt/wayfire/share/wayfire/icons/ridux-files.png
Categories=System;FileManager;
EOF
rm -f rootfs/opt/wayfire/share/applications/ridux-qt-*.desktop \
      rootfs/usr/share/applications/ridux-qt-*.desktop
cat > rootfs/opt/wayfire/share/applications/ridux-settings.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Settings
Comment=Configure RiduxOS
Exec=/opt/wayfire/bin/ridux-settings
Icon=/opt/wayfire/share/wayfire/icons/ridux-settings.png
Categories=Settings;DesktopSettings;
EOF
cat > rootfs/opt/wayfire/share/applications/ridux-terminal.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Terminal
Comment=Ridux terminal
Exec=/usr/bin/ridux-terminal
Icon=/opt/wayfire/share/wayfire/icons/ridux-terminal.png
Categories=System;TerminalEmulator;
EOF
cat > rootfs/opt/wayfire/share/applications/ridux-about.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=About RiduxOS
Comment=System information
Exec=/opt/wayfire/bin/ridux-about
Icon=/opt/wayfire/share/wayfire/icons/ridux-about.png
Categories=System;
EOF
cat > rootfs/opt/wayfire/share/applications/ridux-monitor.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=System Monitor
Comment=Wayfire session status
Exec=/opt/wayfire/bin/ridux-monitor
Icon=/opt/wayfire/share/wayfire/icons/ridux-monitor.png
Categories=System;Monitor;
EOF
cat > rootfs/opt/wayfire/share/applications/ridux-screenshot.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Screenshot
Comment=Capture the Wayland desktop
Exec=/usr/bin/ridux-screenshot
Icon=/opt/wayfire/share/wayfire/icons/ridux-monitor.png
Categories=Utility;
EOF
cat > rootfs/opt/wayfire/share/applications/ridux-lock.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Lock
Comment=Lock the Ridux Wayland session
Exec=/usr/bin/ridux-lock
Icon=/opt/wayfire/share/wayfire/icons/ridux-settings.png
Categories=System;
EOF
cat > rootfs/opt/wayfire/share/applications/ridux-display-settings.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Displays
Comment=Configure Wayland outputs
Exec=/usr/bin/ridux-display-settings
Icon=/opt/wayfire/share/wayfire/icons/ridux-settings.png
Categories=Settings;DesktopSettings;
EOF
cp rootfs/opt/wayfire/share/applications/ridux-*.desktop rootfs/usr/share/applications/
if [ "$FetchDesktopStackFlag" != "1" ] &&
   [ -x rootfs/opt/wayfire/bin/wayfire ] &&
   [ -x rootfs/opt/wayfire/bin/wf-dock ] &&
   [ -x rootfs/usr/bin/waybar ] &&
   [ -f rootfs/usr/lib/x86_64-linux-gnu/libEGL_mesa.so.0 ] &&
   [ -f rootfs/usr/lib/x86_64-linux-gnu/gbm/drm_gbm.so ]; then
  echo "Wayland desktop stack already packaged; skipping Debian payload refresh."
else
  RIDUX_FETCH_DEBIAN_STACK="$FetchDesktopStackFlag" bash tools/package_wayland_desktop_stack.sh rootfs
fi
if [ -L rootfs/usr/bin/thunar ] && [ "`$(readlink rootfs/usr/bin/thunar)" = "thunar" ]; then
  rm -f rootfs/usr/bin/thunar
fi
if [ -x rootfs/usr/bin/thunar ] && [ `$(stat -c%s rootfs/usr/bin/thunar 2>/dev/null || echo 0) -ge 100000 ]; then
  cp -Lf rootfs/usr/bin/thunar rootfs/opt/wayfire/bin/thunar
else
  cp rootfs/opt/wayfire/bin/ridux-launcher rootfs/opt/wayfire/bin/thunar
  cp rootfs/opt/wayfire/bin/thunar rootfs/usr/bin/thunar
fi
test -f rootfs/usr/lib/x86_64-linux-gnu/libEGL_mesa.so.0
test -f rootfs/usr/share/glvnd/egl_vendor.d/50_mesa.json
test -f rootfs/usr/lib/x86_64-linux-gnu/gbm/dri_gbm.so
test -f rootfs/usr/lib/x86_64-linux-gnu/gbm/drm_gbm.so
test ! -L rootfs/usr/lib/x86_64-linux-gnu/gbm/drm_gbm.so
test -f rootfs/usr/lib/x86_64-linux-gnu/dri/kms_swrast_dri.so
mkdir -p rootfs/lib64
for d in rootfs/usr/lib/x86_64-linux-gnu rootfs/opt/wayfire/lib rootfs/opt/wayfire/lib64; do
  [ -d "`$d" ] || continue
  find "`$d" -maxdepth 1 \( -type f -o -type l \) -name '*.so*' -print0
done | while IFS= read -r -d '' f; do
  b="`$(basename "`$f")"
  [ "`$b" = "ld-linux-x86-64.so.2" ] && continue
  cp -Lf "`$f" "rootfs/lib64/`$b" 2>/dev/null || true
done
mkdir -p rootfs/usr/lib/locale rootfs/etc
if [ -d /usr/lib/locale/C.utf8 ]; then
  rm -rf rootfs/usr/lib/locale/C.utf8
  cp -a /usr/lib/locale/C.utf8 rootfs/usr/lib/locale/
fi
if [ -f /usr/lib/locale/locale-archive ]; then
  cp -a /usr/lib/locale/locale-archive rootfs/usr/lib/locale/locale-archive
fi
cat > rootfs/etc/locale.conf <<'EOF'
LANG=C.utf8
LC_ALL=C.utf8
LC_CTYPE=C.utf8
EOF
python3 tools/patch_fontconfig_cache_guard.py || python tools/patch_fontconfig_cache_guard.py
chmod 0755 rootfs/opt/wayfire/bin/* rootfs/usr/bin/ridux-launcher
[ ! -e rootfs/usr/bin/thunar ] || chmod 0755 rootfs/usr/bin/thunar
"@
Invoke-WslSafe $wayfireShellCmd $TimeoutSeconds

Write-Host "== Kernel + overlay =="
$overlayPath = Join-Path $BuildDirFull "initrd-overlay.img"
Remove-Item -LiteralPath $overlayPath -Force -ErrorAction SilentlyContinue
$BuildDirUnix = $BuildDir -replace "\\", "/"
Invoke-WslSafe "if command -v ionice >/dev/null 2>&1; then ionice -c3 nice -n 10 make BUILD_DIR=$(ConvertTo-BashQuoted $BuildDirUnix) kernel-only rootfs/bin/sh -j1; else nice -n 10 make BUILD_DIR=$(ConvertTo-BashQuoted $BuildDirUnix) kernel-only rootfs/bin/sh -j1; fi" $TimeoutSeconds

$overlayCmd = @"
rm -f $(ConvertTo-BashQuoted "$BuildDirWsl/initrd-overlay.img.tmp") $(ConvertTo-BashQuoted "$BuildDirWsl/initrd-overlay.img.list") $(ConvertTo-BashQuoted "$BuildDirWsl/initrd-overlay.img")
{
  printf '%s\n' \
    etc/autoboot.cmd \
    etc/ridux-desktop-shell.disable \
    bin/sh
  [ ! -f rootfs/etc/ridux-wayfire-gpu.enable ] || printf '%s\n' etc/ridux-wayfire-gpu.enable
  [ ! -f rootfs/etc/ridux-wayfire-primary.enable ] || printf '%s\n' etc/ridux-wayfire-primary.enable
  [ ! -f rootfs/etc/ridux-wayfire-strict-stack.enable ] || printf '%s\n' etc/ridux-wayfire-strict-stack.enable
  [ ! -f rootfs/etc/ridux-wayfire-virtio-gpu.enable ] || printf '%s\n' etc/ridux-wayfire-virtio-gpu.enable
  [ ! -f rootfs/etc/ridux-display-refresh-144.enable ] || printf '%s\n' etc/ridux-display-refresh-144.enable
  [ ! -f rootfs/etc/ridux-display-refresh-60.enable ] || printf '%s\n' etc/ridux-display-refresh-60.enable
  [ ! -f rootfs/etc/ridux-wayfire-disable-shader-cache.enable ] || printf '%s\n' etc/ridux-wayfire-disable-shader-cache.enable
  [ ! -f rootfs/etc/ridux-wayfire-vbox-gpu.enable ] || printf '%s\n' etc/ridux-wayfire-vbox-gpu.enable
  [ ! -f rootfs/etc/ridux-wayfire-gles2-software.enable ] || printf '%s\n' etc/ridux-wayfire-gles2-software.enable
  [ ! -f rootfs/etc/ridux-wayfire-pixman.enable ] || printf '%s\n' etc/ridux-wayfire-pixman.enable
  [ ! -f rootfs/etc/ridux-wayfire-full-stack.enable ] || printf '%s\n' etc/ridux-wayfire-full-stack.enable
  [ ! -f rootfs/etc/ridux-wayfire-scanout-mirror.disable ] || printf '%s\n' etc/ridux-wayfire-scanout-mirror.disable
  [ ! -f rootfs/etc/ridux-wayfire-scanout-probe.enable ] || printf '%s\n' etc/ridux-wayfire-scanout-probe.enable
  [ ! -f rootfs/etc/ridux-wayfire-present2d.enable ] || printf '%s\n' etc/ridux-wayfire-present2d.enable
  [ ! -d rootfs/etc/wayfire ] || printf '%s\n' etc/wayfire
  [ ! -d rootfs/etc/xdg ] || printf '%s\n' etc/xdg
  [ ! -d rootfs/etc/wlogout ] || printf '%s\n' etc/wlogout
  [ ! -d rootfs/etc/pipewire ] || printf '%s\n' etc/pipewire
  [ ! -d rootfs/tmp/fontconfig-cache ] || printf '%s\n' tmp/fontconfig-cache
  [ ! -d rootfs/tmp/wayfire-home ] || printf '%s\n' tmp/wayfire-home
  [ ! -d rootfs/mnt/c/Users/SEBA/Downloads/RiduxOS/third_party/wayfire ] || printf '%s\n' mnt/c/Users/SEBA/Downloads/RiduxOS/third_party/wayfire
  [ ! -d rootfs/opt/wayfire ] || printf '%s\n' opt/wayfire
  [ ! -d rootfs/usr/bin ] || printf '%s\n' usr/bin
  [ ! -d rootfs/usr/libexec ] || printf '%s\n' usr/libexec
  [ ! -d rootfs/usr/share/applications ] || printf '%s\n' usr/share/applications
  [ ! -d rootfs/usr/share/dbus-1 ] || printf '%s\n' usr/share/dbus-1
  [ ! -d rootfs/usr/share/wayland-sessions ] || printf '%s\n' usr/share/wayland-sessions
  [ ! -d rootfs/usr/share/pipewire ] || printf '%s\n' usr/share/pipewire
  [ ! -d rootfs/usr/share/wireplumber ] || printf '%s\n' usr/share/wireplumber
  [ ! -d rootfs/ridux/videos ] || printf '%s\n' ridux/videos
} > $(ConvertTo-BashQuoted "$BuildDirWsl/initrd-overlay.img.list")
tar --format=ustar --hard-dereference \
  -cf $(ConvertTo-BashQuoted "$BuildDirWsl/initrd-overlay.img.tmp") \
  -C rootfs -T $(ConvertTo-BashQuoted "$BuildDirWsl/initrd-overlay.img.list")
mv $(ConvertTo-BashQuoted "$BuildDirWsl/initrd-overlay.img.tmp") $(ConvertTo-BashQuoted "$BuildDirWsl/initrd-overlay.img")
rm -f $(ConvertTo-BashQuoted "$BuildDirWsl/initrd-overlay.img.list")
"@
Invoke-WslSafe $overlayCmd $TimeoutSeconds

$kernelPath = Join-Path $BuildDirFull "ridux-kernel.bin"
Assert-MinSize $kernelPath 1MB "kernel"
Assert-MinSize $overlayPath 1MB "overlay"
$overlayMb = Assert-MaxMB $overlayPath $MaxOverlayMB "overlay"
Write-Host "Overlay OK: $overlayMb MB"

Write-Host "== Initrd reducido =="
$initrdPath = Join-Path $BuildDirFull "initrd.img"
$initrdWsl = "$BuildDirWsl/initrd.img"
$tarCmd = @"
rm -f $(ConvertTo-BashQuoted "$initrdWsl.tmp") $(ConvertTo-BashQuoted $initrdWsl)
tar --format=ustar \
  --exclude='./opt/firefox' \
  --exclude='./opt/chromium' \
  --exclude='./bin/*.elf' \
  --exclude='./mnt/c' \
  --exclude='./opt/wayfire' \
  -cf $(ConvertTo-BashQuoted "$initrdWsl.tmp") -C rootfs .
mv $(ConvertTo-BashQuoted "$initrdWsl.tmp") $(ConvertTo-BashQuoted $initrdWsl)
"@
Invoke-WslSafe $tarCmd $TimeoutSeconds
Assert-MinSize $initrdPath 32MB "initrd reducido"
$initrdMb = Assert-MaxMB $initrdPath $MaxInitrdMB "initrd reducido"
Write-Host "Initrd reducido OK: $initrdMb MB"

Write-Host "== ISO Wayfire =="
$isoTree = "$BuildDirWsl/iso-wayfire"
$verifyOverlay = "$BuildDirWsl/verify-overlay.img"
$mkisoCmd = @"
set -e
rm -rf $(ConvertTo-BashQuoted $isoTree)
mkdir -p $(ConvertTo-BashQuoted "$isoTree/boot")
xorriso -osirrox on -indev $(ConvertTo-BashQuoted $BaseIsoWsl) -extract /boot/grub $(ConvertTo-BashQuoted "$isoTree/boot/grub") >/tmp/ridux-wayfire-extract-grub.log 2>&1
if xorriso -indev $(ConvertTo-BashQuoted $BaseIsoWsl) -find /efi.img -type f >/tmp/ridux-wayfire-find-efi.log 2>&1; then
  xorriso -osirrox on -indev $(ConvertTo-BashQuoted $BaseIsoWsl) -extract /efi.img $(ConvertTo-BashQuoted "$isoTree/efi.img") >/tmp/ridux-wayfire-extract-efi.log 2>&1 || true
fi
chmod -R u+w $(ConvertTo-BashQuoted $isoTree) || true
cp $(ConvertTo-BashQuoted "$BuildDirWsl/ridux-kernel.bin") $(ConvertTo-BashQuoted "$isoTree/boot/ridux-kernel.bin")
cp $(ConvertTo-BashQuoted "$BuildDirWsl/initrd.img") $(ConvertTo-BashQuoted "$isoTree/boot/initrd.img")
cp $(ConvertTo-BashQuoted "$BuildDirWsl/initrd-overlay.img") $(ConvertTo-BashQuoted "$isoTree/boot/initrd-overlay.img")
cp grub/grub.cfg $(ConvertTo-BashQuoted "$isoTree/boot/grub/grub.cfg")
rm -f $(ConvertTo-BashQuoted "$OutputIsoWsl.tmp") $(ConvertTo-BashQuoted $OutputIsoWsl)
if [ -f $(ConvertTo-BashQuoted "$isoTree/efi.img") ]; then
  xorriso -as mkisofs -quiet -iso-level 3 -R -J -V RIDUX_WAYFIRE \
    -o $(ConvertTo-BashQuoted "$OutputIsoWsl.tmp") \
    -b boot/grub/i386-pc/eltorito.img \
    -c boot.catalog \
    -no-emul-boot -boot-load-size 4 -boot-info-table --grub2-boot-info \
    -eltorito-alt-boot -e efi.img -no-emul-boot -boot-load-size 5760 \
    $(ConvertTo-BashQuoted $isoTree)
else
  xorriso -as mkisofs -quiet -iso-level 3 -R -J -V RIDUX_WAYFIRE \
    -o $(ConvertTo-BashQuoted "$OutputIsoWsl.tmp") \
    -b boot/grub/i386-pc/eltorito.img \
    -c boot.catalog \
    -no-emul-boot -boot-load-size 4 -boot-info-table --grub2-boot-info \
    $(ConvertTo-BashQuoted $isoTree)
fi
test `$(stat -c%s $(ConvertTo-BashQuoted "$OutputIsoWsl.tmp")) -gt 104857600
mv $(ConvertTo-BashQuoted "$OutputIsoWsl.tmp") $(ConvertTo-BashQuoted $OutputIsoWsl)
rm -f $(ConvertTo-BashQuoted $verifyOverlay)
xorriso -osirrox on -indev $(ConvertTo-BashQuoted $OutputIsoWsl) -extract /boot/initrd-overlay.img $(ConvertTo-BashQuoted $verifyOverlay) >/tmp/ridux-wayfire-verify-extract.log 2>&1
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^opt/wayfire/bin/wayfire$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^opt/wayfire/bin/ridux-session$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^opt/wayfire/bin/ridux-panel$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^opt/wayfire/bin/ridux-visible-shell$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^opt/wayfire/bin/ridux-gpu-ladder$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^opt/wayfire/lib/ridux-client-freeguard.so$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^opt/wayfire/bin/ridux-background$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^opt/wayfire/bin/wf-dock$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^opt/wayfire/bin/thunar$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^mnt/c/Users/SEBA/Downloads/RiduxOS/third_party/wayfire/install/lib/wayfire/libwayfire-shell.so$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^mnt/c/Users/SEBA/Downloads/RiduxOS/third_party/wayfire/install/share/wayfire/metadata/wayfire-shell.xml$'
if [ "$GpuRendererFlag" = "1" ]; then
  tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^etc/ridux-wayfire-gpu.enable$'
fi
if [ "$VBoxGpuFlag" = "1" ]; then
  tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^etc/ridux-wayfire-vbox-gpu.enable$'
fi
if [ "$PixmanRendererFlag" = "1" ]; then
  tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^etc/ridux-wayfire-pixman.enable$'
fi
if [ "$FullDesktopStackFlag" = "1" ]; then
  tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^etc/ridux-wayfire-full-stack.enable$'
fi
if [ "$WayfireVirtioGpuFlag" = "1" ]; then
  tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^etc/ridux-wayfire-virtio-gpu.enable$'
  tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^etc/ridux-display-refresh-144.enable$'
fi
if [ "$DisableShaderCacheFlag" = "1" ]; then
  tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^etc/ridux-wayfire-disable-shader-cache.enable$'
fi
if [ "$Present2DFlag" = "1" ]; then
  tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^etc/ridux-wayfire-present2d.enable$'
fi
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^etc/xdg/waybar/config$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/waybar$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/wofi$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/pipewire$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/wireplumber$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/pipewire-pulse$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/libexec/xdg-desktop-portal$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/libexec/xdg-desktop-portal-wlr$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/swaync$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/swaylock$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/grim$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/slurp$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/ridux-open-launcher$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/ridux-open-files$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/ridux-terminal$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/ridux-power-menu$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/ridux-display-settings$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/ridux-screenshot$'
tar -tf $(ConvertTo-BashQuoted $verifyOverlay) | grep -q '^usr/bin/ridux-lock$'
tar -xOf $(ConvertTo-BashQuoted $verifyOverlay) etc/wayfire/wayfire.ini | grep -q '^plugins = .*command'
tar -xOf $(ConvertTo-BashQuoted $verifyOverlay) etc/wayfire/wayfire.ini | grep -q '^plugins = .*resize'
tar -xOf $(ConvertTo-BashQuoted $verifyOverlay) etc/wayfire/wayfire.ini | grep -q '^command_launcher = /usr/bin/ridux-open-launcher$'
tar -xOf $(ConvertTo-BashQuoted $verifyOverlay) etc/wayfire/wayfire.ini | grep -q '^command_terminal = /usr/bin/ridux-terminal$'
tar -xOf $(ConvertTo-BashQuoted $verifyOverlay) etc/wayfire/wayfire.ini | grep -q '^command_lock = /usr/bin/ridux-lock$'
tar -xOf $(ConvertTo-BashQuoted $verifyOverlay) etc/wayfire/wayfire.ini | grep -q '^command_screenshot = /usr/bin/ridux-screenshot$'
"@
Invoke-WslSafe $mkisoCmd $TimeoutSeconds

Assert-MinSize $OutputIsoFull 100MB "ISO Wayfire"
$isoMb = Assert-MaxMB $OutputIsoFull $MaxIsoMB "ISO Wayfire"
Write-Host "ISO Wayfire OK: $OutputIsoFull ($isoMb MB)"

if ($Boot) {
    if ($Qemu) {
        Write-Host "== Boot QEMU Wayfire =="
        $bootArgs = @(
            "-ExecutionPolicy", "Bypass",
            "-File", ".\scripts\boot-qemu-wayfire.ps1",
            "-IsoPath", $OutputIsoFull,
            "-MemoryMB", $MemoryMB,
            "-CpuCount", $CpuCount,
            "-BootSeconds", $BootSeconds,
            "-SerialLogPath", ".\$BuildDir\qemu-wayfire-safe.log",
            "-ScreenshotPath", ".\$BuildDir\qemu-boot.png"
        )
        if ($UseHostQemu) { $bootArgs += "-UseHostQemu" }
        if ($Interactive) { $bootArgs += "-Interactive" }
        if ($KeepRunning) { $bootArgs += "-KeepRunning" }
        if ($EffectiveGpuRenderer) {
            $bootArgs += "-RequireVirgl"
            $bootArgs += "-RequireGpuLadder"
        }
        & powershell @bootArgs
    } else {
        Stop-RiduxVirtualBoxVm
        Write-Host "== Boot conservador Wayfire =="
        $bootArgs = @(
            "-ExecutionPolicy", "Bypass",
            "-File", ".\scripts\boot-virtualbox.ps1",
            "-IsoPath", $OutputIsoFull,
            "-VmName", "RiduxOS_Wayfire_Safe",
            "-MemoryMB", $MemoryMB,
            "-CpuCount", $CpuCount,
            "-CpuExecutionCap", $CpuExecutionCap,
            "-VramMB", 256,
            "-GraphicsController", "vmsvga",
            "-Mouse", "ps2",
            "-BootSeconds", $BootSeconds,
            "-SerialLogPath", ".\$BuildDir\vbox-wayfire-safe.log"
        )
        if ($EffectiveGpuRenderer) { $bootArgs += "-Accelerate3D" }
        if ($Interactive -or $KeepRunning) { $bootArgs += "-Interactive" }
        if ($KeepRunning) { $bootArgs += "-KeepRunning" }
        & powershell @bootArgs
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Boot Wayfire fallo."
    }

    $auditScript = ".\tools\wayfire_log_audit.py"
    $auditLog = if ($Qemu) { ".\$BuildDir\qemu-wayfire-safe.log" } else { ".\$BuildDir\vbox-wayfire-safe.log" }
    if ((Test-Path $auditScript) -and (Get-Command python -ErrorAction SilentlyContinue)) {
        Write-Host "== Auditoria rapida Wayfire =="
        $auditRenderer = if ($PixmanRendererFlag -eq "1") { "pixman" } else { "gpu" }
        $auditArgs = @($auditScript, $auditLog, "--renderer", $auditRenderer)
        if ($EffectiveFullDesktopStack) { $auditArgs += "--require-full-stack" }
        & python @auditArgs
    }
}

Write-Host "Listo: Wayfire safe ISO generada y verificada."
