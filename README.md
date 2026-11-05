# PalmBolo

Bolo for Palm OS, based on the game rules of WinBolo 2. Drive a tank, capture bases and pillboxes, and send your builder out to build roads, walls, boats and pillboxes.

Version 0.7.0 is single-player against up to 7 bots. Network play over infrared, Bluetooth and Wi-Fi is coming later.

## Devices

Palm OS 3.0 and up, one build for all of them, from 160×160 mono to 320×480 colour. So far it's only been tested on a Tungsten TX in the Frond emulator.

## Install

Download `PalmBolo-0.7.0.prc` from the releases page and install it with HotSync, or beam it from another Palm.

Real Bolo and WinBolo `.map` files work too. Everard Island is built in.

## Playing

From the title screen, New game picks a map, then the game type, hidden mines, time limit, bots, bot level and teams. Leaving a game saves it, and Resume carries on.

Default controls:
- Up and down: faster and slower
- Buttons 1 and 4, or the 5-way left and right: turn
- Button 2 or the 5-way centre: fire
- Button 3: lay a mine
- Tap the map to send the builder, and pick what he builds on the panel

The four buttons can be changed in Settings.

## Building

Needs prc-tools-remix, PilRC, Python 3 and the Palm OS SDK 5r3 in `../palm-os-sdk`.

- `make` builds the release `build/PalmBolo.prc`
- `make DEBUG=1` adds the debug log, timers and self-test
- `make host-test` runs the game rules tests on Linux

## Licence

GPLv3, see LICENSE. The game rules come from WinBolo 2 (https://github.com/john-winbolo/winbolo), also GPLv3.
