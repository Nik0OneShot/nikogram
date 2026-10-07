$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$destination=Join-Path $PSScriptRoot 'assets/source-bundle.zip'
$stream=[IO.File]::Open($destination,[IO.FileMode]::Create)
$bundle=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$false)
$original=[IO.Compression.ZipFile]::OpenRead((Join-Path $PSScriptRoot 'assets/Nikogram-source.zip'))
try {
 foreach($e in $original.Entries){
  if($e.FullName -match '(^|/)(tests?|testing|checks?|samples?|examples?|benchmarks?)(/|$)|\.(log|pdb|obj|sys|exe)$'){throw "Non-distributable payload source entry: $($e.FullName)"}
  $entry=$bundle.CreateEntry($e.FullName,[IO.Compression.CompressionLevel]::Optimal)
  $a=$e.Open();$b=$entry.Open();try{$a.CopyTo($b)}finally{$a.Dispose();$b.Dispose()}
 }
 foreach($rel in & "$PSScriptRoot/Source-Files.ps1"){
  [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($bundle,(Join-Path $PSScriptRoot $rel),'Loader/'+$rel,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
 }
}finally{$original.Dispose();$bundle.Dispose();$stream.Dispose()}
