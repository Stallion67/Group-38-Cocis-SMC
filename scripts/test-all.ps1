# Windows version of test-all.sh. Needs gcc (MSYS2/MinGW) or tcc and Python.
param([string]$Compiler = 'gcc', [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item -ItemType Directory -Force .qa | Out-Null
$f = @('-std=c11', '-Wall', '-Wextra', '-Werror', '-Iinclude')
$tests = @(
  @('test_entrance', 'test/test_entrance.c', 'components/entrance/core.c', 'components/common/system.c'),
  @('test_rooms', 'test/test_rooms.c', 'components/rooms/core.c'),
  @('test_auth_keypad', 'test/test_auth_keypad.c', 'components/auth/session.c', 'components/entrance/kpmenu.c', 'components/common/system.c')
)
foreach ($t in $tests) {
  & $Compiler @f $t[1..($t.Length - 1)] -o ".qa/$($t[0]).exe"; if ($LASTEXITCODE) { throw "build $($t[0])" }
  & ".qa/$($t[0]).exe"; if ($LASTEXITCODE) { throw "$($t[0]) failed" }
}
& $Python test/test_wiring.py; if ($LASTEXITCODE) { throw 'wiring check failed' }
Write-Host 'ALL HOST TESTS PASSED'
