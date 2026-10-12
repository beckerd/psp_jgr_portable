# JGR Portable — @VERSION@

An unofficial native PSP port of Jet Grind Radio. The project is not affiliated
with or endorsed by SEGA or Sony Interactive Entertainment.

No Dreamcast disc image, music, textures, or other original game assets are
included. You must prepare the game data from your own supported copy.

## What you need

- A PSP capable of running homebrew.
- Jet Grind Radio for Dreamcast, **USA MK-51058, V1.005**, in GDI format,
  with every track file referenced by the GDI. Other regions, revisions,
  CHD, and CDI are not supported by this release.
- Desktop **Google Chrome** for the recommended computer preparation method.
- Enough space on your computer and PSP for approximately 1 GB of prepared
  data, in addition to your original disc image on the computer.

This package contains only `README.md`, `Prepare @VERSION@.html`, `LICENSES.txt`
and `EBOOT.PBP`. Keep this EBOOT and preparer together; do not mix them
with files from another release. For source, use GitHub's **Source code (zip)** or **Source code (tar.gz)**
on the [release page](https://github.com/beckerd/psp_jgr_portable/releases/tag/@VERSION@).
Source is not needed to prepare assets or play.

## Recommended: prepare on your computer

1. Extract `JGR-Portable-@VERSION@.zip` to a folder on your computer. Don't run files
   from inside the ZIP.
2. Add your own copy of Jet Grind Radio. A Dreamcast game copy in GDI format
   consists of several files: a small file ending in `.gdi` and track files
   usually ending in `.bin`. Copy the `.gdi` file and all its track files into
   the same folder as `EBOOT.PBP`. Don't rename them or mix files from different
   copies of the game.
3. Open `Prepare @VERSION@.html` in Google Chrome on your computer.
4. Click **Choose folder** and select the folder where you placed everything.
   Allow Chrome to read and save files in that folder when prompted. The page
   will tell you if anything is missing.
5. Click **Prepare Assets**. Leave the page open until it reports success.
   This creates a folder named `data` containing everything the PSP needs.
   All preparation happens on your computer; your game files aren't uploaded.
6. Copy the prepared game to your PSP. Connect your PSP by USB or insert its
   memory card into your computer. Inside `PSP/GAME`, create a folder named
   `JGR_PORTABLE`. Copy `EBOOT.PBP` and the entire `data` folder into it:

   ```text
   PSP/
     GAME/
       JGR_PORTABLE/
         EBOOT.PBP
         data/
   ```

7. Safely eject your PSP or memory card, then launch **JGR Portable** from the
   PSP's Game menu.

You do not need the GDI, track files, or `Prepare @VERSION@.html` on the PSP after
computer preparation. Conversion and copying times depend on your computer
and storage. macOS with Chrome has been tested; Windows and Linux coverage
is still pending.

## Alternative: prepare on the PSP

Copy `EBOOT.PBP`, the GDI, and all referenced tracks into
`PSP/GAME/JGR_PORTABLE/`. Connect external power and launch the game.
The first launch prepares and verifies the data before starting gameplay.

This method is considerably slower; a previous hardware test took about an
hour, and timing varies. Interrupted preparation can resume verified work.
Keep the original disc files available until preparation succeeds. Then you
may remove them; keep `EBOOT.PBP` and the generated `data` folder.

## Saves

Saves are separate from the game folder:

- Memory Stick: `ms0:/PSP/SAVEDATA/JGR_PORTABLE/`
- PSP Go internal storage: `ef0:/PSP/SAVEDATA/JGR_PORTABLE/`

Back up the entire save folder, including `jgr_story.sav` and
`jgr_story.sav.bak`, before changing an existing installation. Preparing game
data does not require deleting saves.

## Controls

| Control | Action |
| --- | --- |
| Analog nub or D-pad | Move; directional prompts while tagging |
| Cross | Jump / confirm |
| Circle | Back in menus |
| L | Spray / recenter camera |
| R | Boost |
| Start | Pause / menu action |
| Select (tap) | Same as Start |
| Hold Select for 3 seconds | Toggle FPS and runtime information |

In **Radio**, use left/right to change songs and Start or Circle to return
to the garage.

## Building from source

Requirements: Git, the [pspdev](https://pspdev.github.io/) toolchain with PSPSDK
(tested with PSP GCC **15.2.0**), GNU make (`gmake` on macOS), and Python
**3.10 or newer** on `PATH`. No game image is needed to compile the port.

```sh
git clone https://github.com/beckerd/psp_jgr_portable.git
cd psp_jgr_portable

# Set this to your installed PSP toolchain.
export PSPDEV=/path/to/pspdev
export PATH="$PSPDEV/bin:$PATH"

# Build the game, bundled helpers and on-device asset preparer.
gmake -C psp -j4                     # -> psp/EBOOT.PBP
```

Use `make` instead of `gmake` where GNU make is installed as `make`. Copy the
built `EBOOT.PBP` to `PSP/GAME/JGR_PORTABLE/` with your supported GDI and all
its tracks, then follow **Prepare on the PSP** above.

For release packaging with the XMB artwork, install Pillow **11.3.0**.
Building the optional browser preparer also requires Emscripten **6.0.12**;
build it from the same checkout as the EBOOT. The
[build guide](https://github.com/beckerd/psp_jgr_portable/blob/main/tools/portable/README.md) covers artwork packaging, source and
binary checks, and building `Prepare @VERSION@.html`.

## Known limitations and troubleshooting

- The Graffiti Maker editor and collectible Graffiti Souls are not
  implemented. The gallery can select available built-in graffiti.
- Performance varies by level and scene. Testing does not yet cover every
  PSP model and firmware combination.
- If preparation lists a missing track, check its filename against the GDI,
  put it in the selected folder, and choose **Try again**.
- If Chrome cannot write files, grant folder access and check available disk
  space. Avoid changing or moving the folder while preparation is running.
- For preparation failures on PSP, check `data/log/setup.log` in the game
  folder. Keep the GDI and tracks available for a repair.
- To verify a suspected damaged installation, remove only
  `data/cache/assets.checked` and launch again. This does not remove saves.

When reporting a problem, include the PSP model, firmware, mission/menu,
what happened, and any available logs.

Project and third-party terms are included in `LICENSES.txt` and in the
preparer's **Licenses & source** disclosure. The preparer links to the
[release tag source](https://github.com/beckerd/psp_jgr_portable/archive/refs/tags/@VERSION@.zip). Asset preparation itself still works offline.

## Trademarks and affiliation

Jet Grind Radio and SEGA are trademarks of SEGA Corporation. PlayStation and PSP
are trademarks of Sony Interactive Entertainment Inc. This is an unofficial
project, not affiliated with, endorsed by, or sponsored by either company.
