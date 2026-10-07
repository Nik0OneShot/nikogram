Nikogram offline loader 0.1.4

Run Nikogram-Loader.exe. Open TF2 separately, wait for detection, then click inject.
Native injection is the only supported method. The method selector and the
loader's manual-mapping execution path have been removed.
No symbol downloads, kernel drivers, memory concealment, header wiping, thread
hijacking or automatic elevation are used. No anti-cheat safety claim is made.

After a successful loading operation the window closes unless keep loader open
is checked. A loader success means the DLL entry point was invoked, not that
all Nikogram signatures and hooks passed their own subsequent startup checks.
Check the in-game interface. After a successful native injection, the loader
allows another explicit injection as soon as a complete module scan confirms
Nikogram is no longer loaded. This works with keep loader open and after reopening
the loader. A deferred or incomplete unload stays blocked while the DLL remains.
Restart TF2 before retrying a failed or uncertain attempt. Historical manual-map
attempts from older loader versions also remain blocked for that TF2 session;
removing the feature does not make an old mapping safe to inject over.

Upgrading from 0.1.0: close the old loader first. Its attempt record has no mode
information. Once the native module is absent, the loader asks once to confirm the old
attempt used native inject and fully unloaded. Only answer Yes for that case.
No restart is required for a confirmed native unload. Subsequent successful
native loads are tracked automatically. Records from 0.1.1 remain compatible.

Everything needed to run is embedded. Native loading materializes the DLL under
%LOCALAPPDATA%\NikogramLoader\payload\<SHA256>\Nikogram.dll. Session lock files
are also local. There are no accounts, downloads, telemetry, updates, servers or
runtime compilation. Settings are session-only. Nothing is written into TF2.

Extract DLL saves the exact sphere-cache batch 5 public DLL. Extract source saves
a complete source bundle: the matching Nikogram source and a Loader directory
with this application's source, branding, payload inputs and dependency sources.
Check files, runtime logs and private Nikogram sources are not included.
Development scripts, preview/check commands and dependency examples are not
part of the application or its source archive.

Rebuild (developers only): Windows x64, Visual Studio C++ tools including ATL,
Windows SDK, and toolset v145. Open NikogramLoader.vcxproj in Visual Studio and
build Release | x64. The project builds its dependency and packages the embedded
source using native MSBuild tasks. No helper scripts or network retrieval are
needed. End users only need Nikogram-Loader.exe, not Visual Studio.

The original Nikogram payload and its matching source ZIP are included as build
inputs in Loader/assets, so the loader can be rebuilt without recompiling TF2
code. If replacing the payload, also replace its matching source and update the
SHA256 constants in Loader.cpp. The source omits the optional font-conversion
script; its generated font header remains included. The supplied source may be modified/rebuilt;
reverse engineering for debugging modifications to LGPL components is permitted.

Nikogram branding supplied by the project owner. Original loader code is under
MIT (LICENSE.txt). Blackbone and AsmJit notices are in THIRD-PARTY-NOTICES.txt and
the dependency source tree. ReWolf WOW64Ext is LGPL-3.0-or-later; its complete
source and license are included under vendor/Blackbone/src/3rd_party/rewolf-wow64ext.
GPL-3.0.txt accompanies that LGPL license. Xenos was a reference only; this
application does not bundle Xenos itself. Blackbone source modifications are
marked in place and summarized in THIRD-PARTY-NOTICES.txt.

Native injection and reinjection were confirmed working by the user in 0.1.1.
This native-only UI cleanup has not been live-injected by developer automation.
The loader does not launch or close TF2.
