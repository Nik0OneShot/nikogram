Nikogram offline loader 0.5.5

Rendering correction in 0.5.5
- Disabled world/sky tinting no longer writes white over native material colors.
- Enabled tinting records the original color and restores only touched materials
  on disable, clean screenshots, level shutdown and unloading.
- Sky tinting selects skybox materials rather than weapon names containing sky.
- Chams/glow preserve exact floating-point color, blend and material overrides
  in local scopes, including nested draws and early returns. Viewmodel glow
  protects its selected material during model drawing. Fullscreen composition
  uses neutral color/blend and releases the model override.
- Acquired chams/glow render contexts are released on all return paths.

Restart TF2 before testing this build, then load it normally. Keep the existing
configuration. In particular, leave world modulation disabled if it was disabled.
Please check ordinary weapons, painted weapons/loadout previews and water during
normal play, deaths/spectating and map changes. Report whether whitening returns.

Validation: Public Release x64 client and loader compiled using MSVC v145.
The prior production modulation code fails the disabled-water-color regression;
the corrected production body passes 182 state/modulation checks. The retained
viewmodel material checks pass both draw routes. Engine calls are simulated in
those checks; these fixes still need live confirmation for the reported symptoms.
Previous 0.5.4 delivery remains available.

Run Nikogram-Loader.exe. Open TF2 separately, wait for detection, then click inject.
Choose Nikogram or Nullcore for the startup menu. Native injection is the only
supported method. No symbol downloads, kernel drivers, memory concealment,
header wiping, thread hijacking or automatic elevation are used. No anti-cheat
safety claim is made. The loader does not launch or close TF2.

After successful loading the window closes unless keep loader open is checked.
A loader success means the DLL entry point was invoked, not that every client
signature and hook passed its subsequent startup checks. Check the in-game menu.
Another explicit injection is allowed only after a complete module scan confirms
Nikogram is no longer loaded. A deferred or incomplete unload remains blocked.
Restart TF2 before retrying a failed or uncertain attempt. Historical manual-map
attempts from older versions remain blocked for that TF2 session.

When upgrading from loader 0.1.0, close the old loader first. Once the native
module is absent, the loader may ask once to confirm that the old attempt used
native injection and fully unloaded. Only answer Yes for that case.

Everything needed to run is embedded. Native loading materializes the DLL under
%LOCALAPPDATA%\NikogramLoader\payload\<SHA256>\Nikogram.dll. Session lock files
are also local. There are no accounts, downloads, telemetry, updates, servers or
runtime compilation. Loader settings are session-only. Nothing is written into
the TF2 installation.

Included client features
- Compact Nikogram menu alongside the original Nullcore layout.
- Independent Legitbot and Ragebot settings, class overrides and inline binds.
- Combined smooth/assistive aiming with region guidance and FOV limits.
- Behind-enemy and On threat conditions, plus radar Dangersense.
- Updated anti-aim animation handling and class/weapon-specific Legit AA.
- Dapper Mann ESP and repeating full-bright photo chams.

Dapper Mann visuals
ESP > Player ESP > Dapper Mann ESP offers Dapper Mann, Dapper Mann - Money and
Giga Mann. The photo stretches to fill the bounding box; other ESP information
is drawn on top. The selector also appears in custom groups and the legacy editor.

The same three photos appear in the model material dropdown for supported chams
targets, including players, viewmodel weapons and hands. Photo materials are
full-bright and repeat 4x4 over the model's existing UVs. Use white tint and full
opacity for the original photo colours. UV seams can stretch or split a face.
All photos are embedded; no external image paths are required.

Version 0.5.4 corrects procedural texture uploads for RGBA, BGRA and BGRX engine
formats, including mip levels. This fixes the black/patterned viewmodel photos.
The project owner confirmed the corrected chams working in game.

Source and rebuilding
Extract DLL saves the matching public 0.5.5 client. Extract source saves the
matching client source plus this loader, its payload inputs and required
dependency sources. Development scripts, tests, logs, dependency examples,
unused driver implementations and private client sources are excluded.

To rebuild the loader: Windows x64, Visual Studio C++ tools including ATL,
Windows SDK and toolset v145. Open NikogramLoader.vcxproj and build Release | x64.
The project builds its dependency and packages source using native MSBuild tasks.
No helper scripts or network retrieval are needed. End users only need the EXE.

Loader/assets contains the client DLL and its matching source ZIP as build
inputs. If replacing the client, replace both and update the SHA256 constants in
Loader.cpp. To rebuild the client, open Nikogram.sln, restore its NuGet packages
and build Release | x64 with NikogramFlavor=Public. The client project targets
v143; the supplied public client was built with a v145 toolset override.
Generated font headers are included; font-conversion scripts are not required.

Licenses
Nikogram branding and photo assets were supplied by the project owner. Original
loader code is MIT (LICENSE.txt). Blackbone and AsmJit notices are in
THIRD-PARTY-NOTICES.txt and the dependency tree. ReWolf WOW64Ext is LGPL-3.0-or-later;
its source, LGPL license and accompanying GPL-3.0.txt are included under
vendor/Blackbone/src/3rd_party/rewolf-wow64ext. Blackbone modifications are marked
in place and summarized in THIRD-PARTY-NOTICES.txt. Xenos was a reference only and
is not bundled. The supplied source may be modified/rebuilt; reverse engineering
for debugging modifications to LGPL components is permitted.
