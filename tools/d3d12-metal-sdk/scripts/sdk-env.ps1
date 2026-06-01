$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkRoot = Resolve-Path (Join-Path $ScriptDir "..")
$RuntimeRoot = Join-Path $SdkRoot "runtime"
$DxmtRuntime = Join-Path $RuntimeRoot "dxmt"
$WineRuntime = Join-Path $RuntimeRoot "wine"
$DefaultPrefix = Join-Path $SdkRoot ".prefix"

$env:METALSHARP_D3D12_SDK_ROOT = $SdkRoot
if (-not $env:METALSHARP_DXMT_RUNTIME) {
  $env:METALSHARP_DXMT_RUNTIME = $DxmtRuntime
}
if (-not $env:WINEPREFIX) {
  $env:WINEPREFIX = $DefaultPrefix
}

$WineExe = Join-Path $WineRuntime "bin/wine"
if (Test-Path $WineExe) {
  if (-not $env:WINE) {
    $env:WINE = $WineExe
  }
  $env:PATH = "$(Join-Path $WineRuntime "bin")$([IO.Path]::PathSeparator)$env:PATH"
}

$DllPaths = @(
  (Join-Path $env:METALSHARP_DXMT_RUNTIME "x86_64-windows"),
  (Join-Path $env:METALSHARP_DXMT_RUNTIME "x86_64-unix")
) | Where-Object { Test-Path $_ }

if ($DllPaths.Count -gt 0) {
  $Prefix = [string]::Join([IO.Path]::PathSeparator, $DllPaths)
  if ($env:WINEDLLPATH) {
    $env:WINEDLLPATH = "$Prefix$([IO.Path]::PathSeparator)$env:WINEDLLPATH"
  } else {
    $env:WINEDLLPATH = $Prefix
  }
}

Write-Output "METALSHARP_D3D12_SDK_ROOT=$env:METALSHARP_D3D12_SDK_ROOT"
Write-Output "METALSHARP_DXMT_RUNTIME=$env:METALSHARP_DXMT_RUNTIME"
Write-Output "WINEPREFIX=$env:WINEPREFIX"
Write-Output "WINE=$env:WINE"
