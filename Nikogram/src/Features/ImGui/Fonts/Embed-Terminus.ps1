param([Parameter(Mandatory=$true)][string]$FontPath)
$ErrorActionPreference = 'Stop'
# Mechanical binary-to-C++ conversion; preserves the complete supplied font.
$fontBytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $FontPath))
$fontLines = [Collections.Generic.List[string]]::new()
$fontLines.Add('#pragma once')
$fontLines.Add('// Generated from the user-supplied TerminusTTF-Bold.ttf. Retains font metadata.')
$fontLines.Add('namespace OneShotFont { inline const unsigned char Data[] = {')
for ($offset=0; $offset -lt $fontBytes.Length; $offset+=24) {
    $end = [Math]::Min($offset+23,$fontBytes.Length-1)
    $fontLines.Add((($fontBytes[$offset..$end] | ForEach-Object { '0x{0:X2}' -f $_ }) -join ',') + ',')
}
$fontLines.Add('}; }')
[IO.File]::WriteAllLines((Join-Path $PSScriptRoot 'TerminusData.h'),$fontLines,[Text.UTF8Encoding]::new($false))
Write-Output "Embedded $($fontBytes.Length) font bytes."
