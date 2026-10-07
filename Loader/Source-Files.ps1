# Explicit distribution list. Never collect the working directory wholesale.
$ErrorActionPreference='Stop'
$required=@(
 'Main.cpp','Loader.cpp','Loader.h','resource.h','resources.rc','loader.manifest',
 'NikogramLoader.vcxproj','Build.ps1','Bundle-Source.ps1','Source-Files.ps1',
 'README.txt','LICENSE.txt','THIRD-PARTY-NOTICES.txt',
 'assets/nikogram.png','assets/Nikogram.dll','assets/Nikogram-source.zip',
 'vendor/Blackbone/LICENSE','vendor/Blackbone/src/BlackBoneDrv/BlackBoneDef.h',
 'vendor/Blackbone/src/BlackBone/BlackBone.vcxproj',
 'vendor/Blackbone/src/3rd_party/DIA/lib/amd64/diaguids.lib',
 'vendor/Blackbone/src/3rd_party/AsmJit/LICENSE.md',
 'vendor/Blackbone/src/3rd_party/rewolf-wow64ext/lgpl-3.0.txt',
 'vendor/Blackbone/src/3rd_party/rewolf-wow64ext/GPL-3.0.txt',
 'vendor/Blackbone/src/3rd_party/rewolf-wow64ext/src/wow64ext.vcxproj'
)
foreach($relative in $required){
 if(!(Test-Path -LiteralPath (Join-Path $PSScriptRoot $relative) -PathType Leaf)){throw "Missing distribution input: $relative"}
 $relative
}
foreach($directory in @('vendor/Blackbone/src/BlackBone','vendor/Blackbone/src/3rd_party')){
 foreach($file in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot $directory) -Recurse -File){
  $relative=$file.FullName.Substring($PSScriptRoot.Length+1).Replace('\','/')
  if($relative -match '(^|/)(tests?|testing|checks?|samples?|examples?|benchmarks?|obj|bin|build|output)(/|$)'){continue}
  if($file.Extension -notin @('.c','.cpp','.h','.hpp','.inl','.asm','.rc','.def')){continue}
  if($relative -notin $required){$relative}
 }
}
