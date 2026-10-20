param([UInt64]$N=20000000, [int]$Threads=8, [int]$MaxThreads=16, [int]$Repeats=7,
      [string]$OutputDirectory='results')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $program=Join-Path $PSScriptRoot 'x64/Release/L1pp.exe'
    if (!(Test-Path $program)) { & ./build.ps1 }
    New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
    & $program --self-test | Tee-Object -FilePath (Join-Path $OutputDirectory 'self-test.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Correctness checks failed' }
    & $program $N $Threads $MaxThreads $Repeats $OutputDirectory |
        Tee-Object -FilePath (Join-Path $OutputDirectory 'console.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Experiment failed' }
} finally { Pop-Location }
