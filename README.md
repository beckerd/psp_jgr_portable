# Test builds

Experimental builds for testing. Stable versions are on the [Releases page](https://github.com/beckerd/psp_jgr_portable/releases).

## JGR Portable 1.0.2

[Download JGR_PORTABLE-1.0.2.zip](https://raw.githubusercontent.com/beckerd/psp_jgr_portable/test_builds/JGR_PORTABLE-1.0.2.zip)

The game is now the main executable. When assets need preparation or checking,
it loads a temporary converter module, then unloads it before gameplay.

This updated test build ignores filenames beginning with a dot when looking
for the GDI, both on PSP and in the browser preparer. It fixes macOS metadata
files being mistaken for a second disc image. The Windows manifest filename
fix is retained.

The ZIP contains `JGR_PORTABLE/EBOOT.PBP`, `Prepare 1.0.2.html`, `README.md`,
and `LICENSES.txt`. Original game assets and source code are not in the game ZIP.

**Updating from 1.0.1 or an earlier 1.0.2 test build:** Replace only `EBOOT.PBP`
in `PSP/GAME/JGR_PORTABLE/`. Keep your existing `data` folder and saves.
For a fresh installation, use the included `Prepare 1.0.2.html` with your own
supported Dreamcast USA MK-51058, V1.005 GDI and all referenced tracks.
Replace any older copy of the 1.0.2 preparer too. Follow the ZIP's README.

**What to test:** Launch the game and confirm normal startup and gameplay.
To exercise asset checking and converter unloading, remove only
`data/cache/assets.checked` and launch again. It should check the assets,
start the game, and skip checking on the next launch. This does not remove
saves. If it fails, retain `data/log/startup-module.log` and `data/log/setup.log`.

**Validation:** This exact EBOOT started fresh preparation on PSP-1000,
wrote 54 files, and cancelled cleanly when stopped early. The converter
returned all measured system memory after unloading. Full onboard conversion
still needs testing. The matching preparer passed full conversion in Chrome
on macOS with hidden GDI files present and every generated asset matching the
reference. This preparer's Windows/Linux hardware testing remains pending.

EBOOT.PBP SHA-256: `d0212567d5cc16fd7bf5c8ea36a3ab01de85a8f495ce003066df8a01ae26ece3`

[Matching source](https://raw.githubusercontent.com/beckerd/psp_jgr_portable/test_builds/JGR_PORTABLE-1.0.2-source.zip) · [Checksums](SHA256SUMS.txt)
