param(
    [string]$BuildDir = ".\build_wsl_firefox",
    [switch]$QuarantineCorrupt
)

$ErrorActionPreference = "Stop"
$BuildFull = [System.IO.Path]::GetFullPath($BuildDir)
$MinLargeArtifact = 100MB

Write-Output "RiduxOS build health"
Write-Output "Build dir: $BuildFull"
Write-Output ""

$busy = Get-Process | Where-Object {
    $_.ProcessName -match 'VirtualBox|VBox|wsl|wslhost|gcc|ld|make|xorriso|grub|qemu'
} | Select-Object ProcessName,Id,CPU,WorkingSet64,StartTime

if ($busy) {
    Write-Output "Procesos relacionados vivos:"
    $busy | Format-Table -AutoSize | Out-String | Write-Output
} else {
    Write-Output "Procesos relacionados vivos: ninguno"
}

Write-Output "Artefactos:"
$names = @("ridux-kernel.bin", "initrd.img", "RiduxOS-Unix.iso", "RiduxOS-Unix.iso.tmp", "initrd.img.tmp")
$bad = @()
foreach ($name in $names) {
    $path = Join-Path $BuildFull $name
    if (-not (Test-Path $path)) {
        Write-Output ("  missing  {0}" -f $name)
        continue
    }
    $item = Get-Item $path
    $kind = "ok"
    if (($name -match 'initrd|iso') -and $item.Length -lt $MinLargeArtifact) {
        $kind = "corrupt?"
        $bad += $item
    }
    Write-Output ("  {0,-8} {1,-22} {2,12} bytes  {3}" -f $kind, $name, $item.Length, $item.LastWriteTime)
}

if ($QuarantineCorrupt -and $bad.Count -gt 0) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $q = Join-Path $BuildFull "quarantine-corrupt-builds"
    New-Item -ItemType Directory -Force -Path $q | Out-Null
    foreach ($item in $bad) {
        $dest = Join-Path $q ($item.Name + "." + $stamp + ".bad")
        Move-Item -LiteralPath $item.FullName -Destination $dest -Force
        Write-Output ("moved {0} -> {1}" -f $item.Name, $dest)
    }
}

