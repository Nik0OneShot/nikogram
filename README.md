<div align="center">

  ## Nikogram

  Nikogram only loads when TF2 is launched with `-insecure`, so it cannot be used on VAC-secured servers.

  Build it yourself from the solution file in Visual Studio.

</div>

## Segmented notifications

Notifications use compact, square-edged black panels with centred, accent-coloured text and an SCP-inspired segmented countdown. Each wrapped line is centred separately. Text, filled segments and the outer border follow the effective outline/accent colour, including changes while a notification is visible. Icons and the old smooth bottom lifetime line are no longer drawn. Explicit per-notification colours do not override the workspace accent in this style.

Up to 24 shaded segments drain from right to left during the configured notification lifetime. Text and bars share an inset, long messages wrap to the available screen width, and toast heights adapt to the text. The existing corner-position, notification-limit, lifetime and slide/stack animation settings remain in use. There is no new setting to enable this style.

The public build excludes the private learning implementation and captured data; distribute only a package produced with `Package-Nikogram.ps1 -Flavor Public`. Notification geometry/countdown tests are under `tests/Notifications`. Isolated tests and successful compilation do not replace an in-game visual check at your chosen text scale and screen resolution.

## Indicator and bind-list styling

Tick status and STORED labels interpolate from 60% of the effective text colour at empty to 100% at full: brightness = 0.6 + 0.4 * clamped charge fraction. At a positive full capacity, the main tick-panel text gains a faint four-direction one-pixel halo (alpha 10/255) and a 5%-toward-white foreground; the meter gains a brighter fill and an inset halo. Text still honours the Interface colour override. Dropping below full removes the glow; zero capacity and speedhack do not count as full. Reserve labels are EMPTY at zero, CHARGING for a partial reserve, and FULL at capacity, independent of doubletap eligibility.

A DT READY / DT NOT READY line uses the same 12px Courier New label font as the right-hand reserve status and STORED, positioned halfway between them with matching right alignment. It checks the active doubletap setting, weapon validity, available shift, wait/recharge/warp/speedhack state, rocket-jump automation and attack/reload eligibility without requiring attack to be held already. This detail stays crisp without a halo and is dimmed when unavailable, independently of reserve charge. The panel grows vertically to allocate a real line plus padding; its drag bounds use the updated size.

Ticks uses the compact SCP-style status panel: TICKS/reserve header, modest bold count with STORED label, then a segmented bar with visible empty cells. It shares crithack's Courier New fonts and compact spacing. Text honours the Interface text-colour override; borders and filled cells use the outline/accent. FULL represents a full positive reserve, not a guarantee that doubletap will execute. WARPING and SPEEDHACK override the normal reserve label; speedhack shows its multiplier instead of a stored count. The editor uses the full panel dimensions, no extra border or label, and reserves the taskbar strip. No tick-shifting or charging mechanics were changed.

Interface now offers **Custom text colour**. Off/cleared means no override: all Nikogram-rendered text follows the effective outline/accent colour. Enable it to choose a separate RGB colour; **Clear** restores accent-following. Save using the Interface preset's save button. The setting is captured in interface presets and workspace preferences, with old presets defaulting to no override. This global policy includes previously group-coloured ESP names and status/log text; native TF2 UI, icon glyphs, text outlines, images, borders and bars are not recoloured. Disabled/faded text retains its alpha.

Menu text uses 3x horizontal / 2x vertical glyph oversampling with pixel snapping disabled. Overlay fonts retain grayscale anti-aliasing; the compact crit panel uses clean unoutlined text. Its base minimum width is now 196px, padding 6px, gaps 3px and bar height 10px, expanding for longer messages. Tests under tests/TextTheme cover colour resolution, interface preset round trips, old preset defaults and persisted preferences. Visual smoothing and actual in-game layout still require runtime review.

Crit hack uses the compact status-panel layout: CRIT/readiness header, zero-padded available/capacity count with a STORED label, shaded segmented capacity bar, and a NEXT CHARGE footer. The count is 16px bold at base scale, labels 12px, with 8px padding and 4px row gaps. All panel text follows the effective outline/accent colour; secondary labels use a dimmer shade of that same colour. Clean Courier New fonts omit outlines and drop shadows in this panel. Cooldown, streaming, damage-restricted and server-disabled states replace the appropriate status/footer fields, while compatibility, damage and desync details appear as separate rows when relevant. A filled capacity bar does not override a ban or cooldown.

Menus, notifications, binds, statistics, ESP and other overlay text now use Courier New, with bold headings. This matches the main UI font family defined in SCP: Containment Breach's Main.bb (https://github.com/Regalis11/scpcb/blob/master/Main.bb), not its specialist digital or handwritten fonts. Windows-installed cour.ttf and courbd.ttf supply ImGui text; no font files are redistributed. If unavailable, ImGui falls back to its built-in font. Icon glyphs are unchanged. Font rendering differs between game engines, so this is a family match, not a pixel-identical rendering claim.

Every bind has its own black background and accent outline. Under the Binds tab's Settings section, enable **Horizontal binds list** for side-by-side entries, wrapping onto another row when the screen width is exhausted. The default remains vertical. This option is saved in normal configs. Existing bind visibility rules, dragging, and editing controls remain available.

Active bind cards show a steady accent-coloured halo, brighter inner edge and subtle tinted fill. Hold binds glow while active; toggles glow while toggled on. Disabled binds and children of inactive/disabled parents do not glow. The effect uses the same card renderer in vertical and horizontal layouts and leaves text-colour overrides unchanged.

Active bind names, modes and keys also have a soft text halo with a crisp foreground. Binds > Settings offers **Active bind text glow** (on by default), **Custom bind glow colour** (off by default) and **Bind glow colour** when custom is enabled. Automatic glow mixes the current accent 45% toward white. Custom glow uses the chosen RGB colour. This intentional, scoped exception to the global text colour affects only glowing active-bind text; inactive text still follows the Interface text preference. Settings save with normal gameplay/visual configs. The card glow remains unchanged.

Bind-list dragging clamps the live window position before drawing, keeping the panel inside the screen without resizing it. While the menu is open, binds and crit hack reserve the taskbar strip (top or bottom). Crit hack's drag handle now matches the actual panel size and no longer overlays a label on its status text. Geometry tests cover screen edges, both taskbar placements, oversized extents and multiple scales; live dragging still needs an in-game check.
