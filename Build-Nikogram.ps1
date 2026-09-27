param([ValidateSet('Public','Private')][string]$Flavor='Public',[string]$LogPath="$PSScriptRoot/build-$Flavor.log")
$ErrorActionPreference='Stop'
$msbuild='C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe'
if(-not (Test-Path -LiteralPath $msbuild)){throw 'Set the MSBuild path for your installed Visual Studio toolchain.'}
& $msbuild "$PSScriptRoot/Nikogram.sln" '/m:4' '/p:Configuration=Release' '/p:Platform=x64' '/p:PlatformToolset=v145' "/p:NikogramFlavor=$Flavor" '/nologo' '/v:minimal' "/flp:logfile=$LogPath;verbosity=normal"
if($LASTEXITCODE -ne 0){throw "Build failed: $LASTEXITCODE"}
