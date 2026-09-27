# Public source refresh

This update imports the audited public source package used for the spectator-highlight build.
Private neural-network implementations, datasets, profiles, recordings, logs, local
settings and compiled DLLs are not included. Conditional public no-op integration
stubs remain; they do not implement private learning.

Recent public UI changes include:

- Separate gameplay-config borders and higher-priority Interface border overrides.
- Default Interface borders defer to the gameplay config; a Use config button resets the override.
- Independent active and inactive text colours, defaulting to accent and 65% accent.
- Text Title Color for dropdown headings, slider labels and section headings,
  defaulting to a 20% blend from accent toward white.
- Compact spectator layouts with resizing and text highlighting for confirmed
  local-player spectator targets in vertical and horizontal layouts.
- Embedded OneShot Terminus font and updated indicator styling.

The source package's x64 Release/Public build was verified locally with MSVC v145,
with zero warnings or errors. Isolated theme persistence, spectator classification,
layout and highlight-target checks passed. In-game appearance remains separate
from compiler/test verification.

The existing GitHub workflow now names Nikogram.sln explicitly and builds only the
Public flavor. GitHub's hosted toolchain and the additional build variants still
need their own CI results; the local Release build does not verify those variants.

See BUILD-FLAVORS.md for packaging boundaries. Some narrative sections in README.md
describe earlier UI iterations; this document describes the latest imported UI.
