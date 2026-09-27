# Private and public releases

Public is the default build flavor. It compiles without the private learning implementation. Separate output and intermediate directories prevent accidental reuse between flavors.

With Visual Studio Community 2026 C++ tools (v145) installed and the solution's NuGet packages restored:

```powershell
./Build-Nikogram.ps1 -Flavor Private
./Build-Nikogram.ps1 -Flavor Public
./Package-Nikogram.ps1 -Flavor Private -Destination C:/my-new-private-package
./Package-Nikogram.ps1 -Flavor Public -Destination C:/my-new-public-package
```

Use an unused destination directory. The packaging script checks the DLL flavor and excludes saved learning data, logs, settings, PDBs, analysis artifacts and build outputs from source archives. Public source additionally excludes the private implementation, its tests and private guide. Only the safe integration stubs/conditional calls remain. The public source cannot build the private implementation because those files are not supplied.

For public distribution, share ONLY the explicit public package. Do not upload the complete working directory, private source archive, private DLL or game-data directory. Do not use older packaging scripts, which predate the private/public separation. A Git ignore rule is not a security boundary and does not remove previously committed material from history.

Private releases are intended for the owner and their chosen friend. This separation is not DRM, encryption or access control; anyone receiving the private DLL/source/data can copy it.

The local public build used during development is an isolation check, not a request to publish a release. No upload is performed.
