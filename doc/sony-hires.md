# Sony high resolution

On older Clié devices, PalmBolo opens Sony HR Library and switches to 320×320.
Tile, sprite, mask, overview and composed windows use physical pixel sizes.
The game uses 16-pixel art; pen events and native forms retain logical 160
coordinates. Native drawing selects Sony's full-size fonts and restores the
ordinary font before returning to forms. The title has its own 320-pixel bitmap.
Devices with Palm's high-density window manager use the existing Palm API.
If the Sony library cannot load, open or change mode, drawing stays 160×160.
The original Sony display mode and depth are restored on exit. The app closes
only its own open reference and removes a library only if it loaded it and
HRClose reports that no references remain.

Sony clip origins and extents must be even (Programmer's Companion, page 81).
Repaint regions expand to those boundaries before drawing terrain and marks;
the same expanded regions are copied to the display. The game view boundary
is even. Overview tests cover single-pixel and five-pixel updates at its edges.

The ABI declarations in `src/sonyhr.h` come from Sony CLIE SDK 5.0,
`Sony SDK Support/R5.0/Incs/Libraries/SonyHRLib.h`, dated 2002-07-17.
Library name and creator come from `System/SonySystemResources.h`.
The Programmer's Companion, chapter 5, documents drawing, fonts, offscreen
windows, mode changes and library ownership. The minimal declarations are
kept here; builds need only the existing Palm OS SDK and prc-tools.

Local source archive:
`../dev-palmos/palmos-sdk/!sdk-extra/palmos-cliesdk50e.zip`

SHA256: `e9922f555e2a392e4a263175439f8363d32dd7c104487981a8b539cd7b54a17d`.
Sony's SDK files retain Sony's copyright and are not redistributed here.
No trap numbers were inferred from a ROM or executable.

`make screen-test` compiles the production detection, screen setup, drawing
adapter and pen handler with controlled SDK calls, with ASan/UBSan. It checks
capabilities, failures, ownership, ordinary versus native drawing, fonts,
offscreen sizes, clipping, rectangle copies and pen geometry. This checks
call selection and arguments; it does not simulate Sony's rasteriser.
Supported Clié Frond flows are on the before-release list. Physical Sony
rendering remains untested.
