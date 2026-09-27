param(
    [ValidateSet('Public','Private')][string]$Flavor='Public',
    [Parameter(Mandatory=$true)][string]$Destination
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$destinationPath=[System.IO.Path]::GetFullPath($Destination)
if(Test-Path -LiteralPath $destinationPath){throw 'Choose a new destination directory; packages are never overwritten.'}
$dll=Join-Path $PSScriptRoot "output/x64/Release/$Flavor/Nikogramx64Release-$Flavor.dll"
if(-not(Test-Path -LiteralPath $dll)){throw "Build $Flavor first."}
$marker='nikogram-private-observations'
$dllText=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($dll))
if($dllText.Contains($marker) -ne ($Flavor -eq 'Private')){throw 'DLL flavor audit failed.'}
New-Item -ItemType Directory -Path $destinationPath | Out-Null
$archivePath=Join-Path $destinationPath "Nikogram-$Flavor-source.zip"
$archive=[IO.Compression.ZipFile]::Open($archivePath,[IO.Compression.ZipArchiveMode]::Create)
$count=0
try {
    $rootFiles=@('Nikogram.sln','Build-Nikogram.ps1','Package-Nikogram.ps1','BUILD-FLAVORS.md','LICENSE.md','README.md','.gitignore','.gitattributes','default.json')
    if($Flavor -eq 'Private'){$rootFiles+='PRIVATE-LEARNING.md'}
    $candidates=@(Get-ChildItem -LiteralPath "$PSScriptRoot/Nikogram" -Recurse -File)
    $candidates+=@(Get-ChildItem -LiteralPath "$PSScriptRoot/tests" -Recurse -File)
    foreach($name in $rootFiles){$candidates+=Get-Item -LiteralPath (Join-Path $PSScriptRoot $name)}
    foreach($file in $candidates){
        $relative=$file.FullName.Substring($PSScriptRoot.Length+1).Replace('\','/')
        # Never distribute captured data, local settings, logs, binaries, build output or analysis.
        if($relative -match '(^|/)(bin|obj|build|output|packages|Logs|PrivateLearning|LearningData|VisitTrainingRuns|VisitEvaluationRuns|EngineComparisonRuns|TrainingRuns|EvaluationRuns|Recordings|\.git|\.vs)(/|$)' -and $relative -notmatch '^tests/PrivateLearning/[^/]+$'){continue}
        if($file.Extension -notin @('.h','.hpp','.cpp','.c','.inl','.vcxproj','.filters','.config','.props','.targets','.lib','.sln','.ps1','.md','.json','.gitignore','.gitattributes')){continue}
        if($file.Extension -eq '.json' -and $relative -ne 'default.json'){continue}
        if($Flavor -eq 'Public' -and $relative -match '(^|/)(Private|PrivateLearning)(/|$)'){continue}
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$file.FullName,$relative,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
        ++$count
    }
} finally {$archive.Dispose()}
$audit=[IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    foreach($entry in $audit.Entries){
        if($entry.FullName -match '\.(learn|bin|pdb|log|bak|tmp|dll)$'){throw "Forbidden archive entry: $($entry.FullName)"}
        if($Flavor -eq 'Public' -and $entry.FullName -match '(^|/)(Private|PrivateLearning)(/|$)|PRIVATE-LEARNING'){throw 'Private source leaked into public archive.'}
    }
} finally {$audit.Dispose()}
$targetDll=Join-Path $destinationPath "Nikogram-$Flavor-x64-Release.dll"
Copy-Item -LiteralPath $dll -Destination $targetDll
Copy-Item -LiteralPath "$PSScriptRoot/BUILD-FLAVORS.md" -Destination $destinationPath
if($Flavor -eq 'Private'){Copy-Item -LiteralPath "$PSScriptRoot/PRIVATE-LEARNING.md" -Destination $destinationPath}
Write-Output "Packaged $Flavor; $count source files; data and binary flavor audits passed."
Get-FileHash -LiteralPath $targetDll,$archivePath -Algorithm SHA256
