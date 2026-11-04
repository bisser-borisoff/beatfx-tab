# Beat FX Tab

**Reclaim your screen. Keep your Beat FX in sight.**

An [AZ-MODS](https://github.com/Kyle-Hosman/xdj-az-mods) community mod for the XDJ-AZ.

If you want your screen space back for beat and phrase matching, and outstanding situational
awareness of your Beat FX, this is your mod.

The BEAT FX panel takes a big part of the waveform screen while it is open. Collapsed, its tab
shows only an arrow. Beat FX Tab turns that collapsed tab into a live display:

- the selected effect, as on the knob: DELAY, ECHO, PING PONG, SPIRAL, REVERB, ...
- its beat value in a white box: 1/16 to 64 beats, LENGTH % for REVERB
- **X-PAD** in the box while your finger plays the effect on the X-PAD
- **off:** grey, with the name underlined in the effect group's X-PAD colour
- **on:** the whole tab in that colour

| Colour | Effects |
|---|---|
| blue | DELAY, ECHO, PING PONG, SPIRAL, HELIX |
| orange | REVERB |
| yellow | FLANGER, PHASER, FILTER, TRIPLET FILTER, TRANS |
| green | ROLL, TRIPLET ROLL |
| purple | MOBIUS |

Tap the tab where you always do, on the white box or the arrow, to open the stock panel. The
label steps aside while the panel is open and comes back when you close it.

## Where it shows

| View | |
|---|---|
| WAVEFORM 2 DECK | yes |
| 2 waveforms with 4 decks below | yes |
| 4 DECK | not yet: the stock tab shows |

## Safe by design

- **Display only.** It reads the mixer's Beat FX report (which effect, which beat, on or off) and
  where you touch the screen. Every frame and every touch goes on to the deck unchanged.
- **No hooks**, so it does not depend on a firmware version. No files, no network, no threads of
  its own.
- It repaints at most 10 times a second, and only when what it shows changes.
- When something is unknown, it shows the stock tab rather than a guess.

## Tested

- XDJ-AZ firmware 2.01 with AZ-MODS 1.2.7.
- Together with AZ-MODS' Phase Meter, 60 FPS Waveforms, 60 FPS UI and Stems.
- **Not tested** with AZ-MODS' Drum Roll, or with community mods that use the X-PAD or the Beat
  FX section. If you use one, try it at home before a gig, and tell us what you see in an issue or
  on Discord. Reports and pull requests are welcome.

## Install

On the deck: **Mod Menu > Community Mods > Repositories > Add**, enter
`bisser-borisoff/beatfx-tab`, then **Browse > Beat FX Tab > Install**.

## Build

With the [AZ-MODS SDK](https://github.com/Kyle-Hosman/az-mods-sdk) checked out next to this
folder:

```sh
../az-mods-sdk/tools/azmod-pack build .   # build in the SDK container, then validate
make unit                                  # host unit tests (clang, ASan + UBSan, TSan)
make test AZ_SDK=../az-mods-sdk            # the SDK's test harness: plain, ASan + UBSan, TSan
```

Builds are reproducible: the `.so` in a release has the same SHA-256 as one you build from the
same tag with the same SDK commit (`SDK_REF` in `.github/workflows/release.yml`).

| File | |
|---|---|
| `beatfx-tab.c` | the mod: filters, visibility, paced repaints |
| `fx_decode.c` | the mixer's Beat FX report: which bytes mean what |
| `panel_track.c` | follows the stock panel (open or closed) from the taps |
| `label_draw.c` | paints the label |
| `geometry.h` | every position and colour, calibrated on the deck |

## License

GPL-3.0-only. See [LICENSE](LICENSE).

Not affiliated with AlphaTheta, Pioneer DJ or rekordbox; trademarks belong to their owners.
