param(
    [switch]$Apply,
    [switch]$AlsoDeleteActiveBuild,
    [string]$KeepActiveBuild = "build_codex_gpu"
)

$ErrorActionPreference = "Stop"

function Get-WorkspaceRoot {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
}

function Assert-InWorkspace {
    param(
        [Parameter(Mandatory=$true)][string]$Workspace,
        [Parameter(Mandatory=$true)][string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    $prefix = $Workspace + [IO.Path]::DirectorySeparatorChar
    if ($resolved -eq $Workspace -or -not $resolved.StartsWith($prefix)) {
        throw "Refusing to touch outside workspace: $resolved"
    }
    return $resolved
}

function Get-ItemSize {
    param([Parameter(Mandatory=$true)][System.IO.FileSystemInfo]$Item)

    if ($Item.PSIsContainer) {
        $sum = (Get-ChildItem -LiteralPath $Item.FullName -Recurse -Force -File -ErrorAction SilentlyContinue |
            Measure-Object -Property Length -Sum).Sum
        return [int64]$sum
    }
    return [int64]$Item.Length
}

$workspace = Get-WorkspaceRoot
$targets = New-Object System.Collections.Generic.List[string]

Get-ChildItem -LiteralPath $workspace -Force -Directory |
    Where-Object {
        $_.Name -eq "build" -or
        $_.Name -eq "out" -or
        $_.Name -like "build_*"
    } |
    ForEach-Object {
        if ($_.Name -eq $KeepActiveBuild -and -not $AlsoDeleteActiveBuild) {
            return
        }
        $targets.Add($_.FullName)
    }

$exactNames = @(
    "RiduxOS.iso",
    "build_err.txt",
    "build_err_tail.txt",
    "build_wayfire_safe-runlog.err",
    "build_wayfire_safe-runlog.out",
    "build_wayfire_safe-runlog.txt",
    "err.log",
    "err2.log",
    "src.zip",
    "vbox-boot.png",
    "-",
    "-p",
    " ; printf 0x%xn -1082896384; done",
    "  true; else echo missing; fi; done",
    "%ln  grep -E thunar",
    "%ln  sort",
    ".VSCodeCounter",
    "rootfs/mnt",
    "rootfs/tmp/fontconfig-cache",
    "rootfs/tmp/kde-home",
    "rootfs/tmp/kde-tmp",
    "rootfs/tmp/kde-var-tmp",
    "rootfs/tmp/wayfire-build",
    "rootfs/tmp/wayfire-home",
    "rootfs/var/lib"
)

foreach ($name in $exactNames) {
    $path = Join-Path $workspace $name
    if (Test-Path -LiteralPath $path) {
        $targets.Add($path)
    }
}

$rows = New-Object System.Collections.Generic.List[object]
foreach ($target in ($targets | Select-Object -Unique)) {
    $resolved = Assert-InWorkspace -Workspace $workspace -Path $target
    $item = Get-Item -LiteralPath $resolved -Force
    $bytes = Get-ItemSize -Item $item
    $rows.Add([pscustomobject]@{
        Path = $resolved.Substring($workspace.Length + 1)
        Bytes = $bytes
        MB = [math]::Round(([double]$bytes / 1MB), 2)
    })
}

$total = [int64](($rows | Measure-Object -Property Bytes -Sum).Sum)
$rows | Sort-Object Bytes -Descending | Format-Table -AutoSize
"TOTAL_GB={0}" -f ([math]::Round(([double]$total / 1GB), 2))

if (-not $Apply) {
    ""
    "Dry run only. Re-run with -Apply to delete these generated artifacts."
    return
}

foreach ($target in ($targets | Select-Object -Unique)) {
    $resolved = Assert-InWorkspace -Workspace $workspace -Path $target
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

"Deleted generated workspace artifacts."
