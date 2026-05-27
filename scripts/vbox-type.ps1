# Send an ASCII string to the running VirtualBox VM as PS/2 set-1 scancodes,
# followed by Enter. Used to drive the in-VM terminal during automated tests
# of new Ring 3 apps.
param(
    [Parameter(Mandatory=$true)] [string] $VmName,
    [Parameter(Mandatory=$true)] [string] $Text,
    [switch] $NoEnter
)

$vbm = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'

# US scancode tables (set 1). Make+Break codes as VBoxManage expects them.
$lc = @{
  'a'='1e';'b'='30';'c'='2e';'d'='20';'e'='12';'f'='21';'g'='22';'h'='23';
  'i'='17';'j'='24';'k'='25';'l'='26';'m'='32';'n'='31';'o'='18';'p'='19';
  'q'='10';'r'='13';'s'='1f';'t'='14';'u'='16';'v'='2f';'w'='11';'x'='2d';
  'y'='15';'z'='2c';
  '0'='0b';'1'='02';'2'='03';'3'='04';'4'='05';'5'='06';'6'='07';'7'='08';
  '8'='09';'9'='0a';
  ' '='39';'-'='0c';'='='0d';'['='1a';']'='1b';';'='27';"'"='28';
  '`'='29';'\'='2b';','='33';'.'='34';'/'='35';
}

$shift = @{
  ':'='27';'?'='35';'_'='0c';'+'='0d';'!'='02';'@'='03';'#'='04';'$'='05';
  '%'='06';'^'='07';'&'='08';'*'='09';'('='0a';')'='0b'
}

function Press([string]$code) {
  $break = [Convert]::ToString([Convert]::ToInt32($code,16) -bor 0x80, 16)
  if ($break.Length -lt 2) { $break = '0' + $break }
  & $vbm controlvm $VmName keyboardputscancode $code $break | Out-Null
}

foreach ($ch in $Text.ToCharArray()) {
  $key = $ch.ToString().ToLower()
  if ($lc.ContainsKey($key)) {
    if ([char]::IsUpper($ch)) {
      # Shift down + key + key release + shift up
      & $vbm controlvm $VmName keyboardputscancode 2a $lc[$key] ([Convert]::ToString([Convert]::ToInt32($lc[$key],16) -bor 0x80, 16)) aa | Out-Null
    } else {
      Press $lc[$key]
    }
    Start-Sleep -Milliseconds 25
  } elseif ($shift.ContainsKey($ch.ToString())) {
    $code = $shift[$ch.ToString()]
    $break = [Convert]::ToString([Convert]::ToInt32($code,16) -bor 0x80, 16)
    if ($break.Length -lt 2) { $break = '0' + $break }
    & $vbm controlvm $VmName keyboardputscancode 2a $code $break aa | Out-Null
    Start-Sleep -Milliseconds 25
  } else {
    Write-Warning "skip unmapped char '$ch'"
  }
}

if (-not $NoEnter) {
  Press '1c'  # Enter
}
