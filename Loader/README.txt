Nikogram 0.6.16 - Freelook
IMPLEMENTED AND LOCALLY TESTED. NOT VERIFIED IN-GAME BY CODEX.
Prepared only: no game launched, controlled, injected into, or reloaded.

Use:
Misc > Freelook: click Bind and assign your preferred key. Hold is the default.
Hold the key and look around; normal manual aim and WASD orientation remain at
the direction captured on activation. Release to return. Shots still use normal
gameplay aim, NOT the detached camera. Existing aimbot/anti-aim processing remains
in control of its own behavior; freelook does not disable or reconfigure it.

Defaults:
- Freelook disabled/unbound until you assign a key.
- Continuous horizontal rotation, including repeated complete turns.
- Vertical view stops at +/-89 degrees rather than flipping upside down.
- Limit neck movement OFF. When enabled: +/-90 horizontal, +/-60 vertical from
  activation direction, with adjustable limits.
- Return Instant. Optional Smooth has an adjustable duration (default 0.20 sec).
- Holding again during Smooth return resumes from the current camera position.
- First-person and existing third-person modes both use the detached direction.
  Third-person orbit offset/collision are calculated after the camera angle.
- OptiFine Zoom stays usable without resetting freelook; scaled sensitivity is
  applied once. Scope/normal FOV remains owned by the existing zoom/FOV handling.
- Menu/cursor, focus loss, death/spectating, disconnect/map shutdown, unload and
  forced taunt/kart/ghost/control-stun views clear freelook.

Implementation:
Mouse deltas are intercepted at the existing ApplyMouse hook after native input
filtering, sensitivity/acceleration and client mouse override. Freelook consumes
these deltas using m_yaw/m_pitch (including inversion), rather than calling a
native mouse handler that can mutate movement or third-person camera globals.
The ordinary mouse handler is unchanged when freelook is not held.
Normal input angles are fixed before the command snapshot; gameplay systems run
afterwards. Camera angles are applied before third-person orbit and OptiFine Zoom.
Smooth return follows live normal aim; normal mouse aiming resumes on release.

Verification:
- 18,048 focused checks using the production policy and mechanically extracted
  Visuals/view-hook functions with synthetic input/render interfaces.
- Covers unlimited seam-crossing, limits/large deltas, pitch poles and inversion,
  fixed live aim/movement/action fields, instant/smooth release, frame-rate
  independence, re-hold, zoom on/off in both perspectives, camera hook order,
  repeated/auxiliary renders, player identity and eligibility resets.
- 39,371 existing zoom policy/binding regressions pass.
- 9,146 existing Edge selection/body model checks pass.
- 43,121 existing non-Edge body model regressions pass.
- Public Release x64 client and loader built with VS Community 2026 / MSVC v145.
- All 823 curated source entries verified: ten existing files changed plus
  FreelookPolicy.h. All other archived files retained byte-for-byte, including
  the accepted 0.6.15 AntiAim/EdgeCoverPolicy/BodyYawPolicy, ZoomPolicy, Dapper
  and screenshot implementation. Curated root metadata is retained unchanged.
- Source fixtures and test policy headers checked against the compiled source.

Limits:
Synthetic interfaces are not a running TF2 engine. No native input-hook,
first-person viewmodel/crosshair presentation, third-person wall collision,
server-side model orientation, or controller-input acceptance test was run.
Mouse-driven freelook is implemented; joystick/controller camera look is not.
Existing movement automation, aim assistance and anti-aim retain their own
behavior; freelook only detaches manual camera input. No native performance
or loader-execution claim is made.
Source review reference (not binary/runtime verification):
https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/in_mouse.cpp

If an earlier Nikogram DLL is loaded, use a fresh TF2 session when you choose to
test; do not stack builds. The loader's existing -insecure requirement remains.
Matching loader, DLL, complete source bundle and local verification are included.
