param([string]$LogPath="$PSScriptRoot/build-loader.log")
$ErrorActionPreference='Stop'
$msbuild='C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe'
if(!(Test-Path -LiteralPath $msbuild)){throw 'Visual Studio C++ build tools are needed to rebuild, not to run the EXE.'}
Push-Location $PSScriptRoot
try {
 & "$PSScriptRoot/Bundle-Source.ps1"
 & $msbuild vendor/Blackbone/src/BlackBone/BlackBone.vcxproj /m:4 /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /p:PostBuildEventUseInBuild=false /p:OutDir=../../../lib/ "/p:IntDir=$PSScriptRoot/build/blackbone/" /nologo /v:minimal '/flp:logfile=blackbone-build.log;verbosity=normal'
 if($LASTEXITCODE){throw 'Dependency build failed'}
 & $msbuild NikogramLoader.vcxproj /m:4 /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /nologo /v:minimal "/flp:logfile=$LogPath;verbosity=normal"
 if($LASTEXITCODE){throw 'Loader build failed'}
}finally{Pop-Location}
