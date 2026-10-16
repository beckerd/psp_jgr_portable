# Test builds

Experimental builds for testing. Stable versions are on the [Releases page](https://github.com/beckerd/psp_jgr_portable/releases).

## JGR Portable 1.0.2

[Download JGR_PORTABLE-1.0.2.zip](https://raw.githubusercontent.com/beckerd/psp_jgr_portable/test_builds/JGR_PORTABLE-1.0.2.zip)

The game is now the main executable. When assets need preparation or checking,
it loads a temporary converter module, then unloads that module before gameplay.
This removes the previous setup-to-game executable handoff.

The ZIP contains `JGR_PORTABLE/EBOOT.PBP`, `Prepare 1.0.2.html`, `README.md`,
and `LICENSES.txt`. Original game assets and source code are not in the game ZIP.

**Updating from 1.0.1:** Replace only `EBOOT.PBP` in `PSP/GAME/JGR_PORTABLE/`.
Keep your existing `data` folder and saves. For a fresh installation, use the
included `Prepare 1.0.2.html` with your own supported Dreamcast USA
MK-51058, V1.005 GDI and all referenced tracks. Follow the ZIP's README.

**What to test:** Launch the game and confirm normal startup and gameplay.
To exercise asset checking and converter unloading, remove only
`data/cache/assets.checked` and launch again. It should check the assets,
start the game, and skip checking on the next launch. This does not remove
saves. If it fails, retain `data/log/startup-module.log` and `data/log/setup.log`.

**Validation:** This exact EBOOT passed existing-asset verification and two
launches on PSP-1000. The converter returned all measured system memory after
unloading. The matching preparer passed full conversion in Chrome on macOS
with every generated asset matching the reference. Full disc conversion on PSP
hardware, and this preparer's Windows/Linux testing, remain pending.

EBOOT.PBP SHA-256: `c2466e0b7318624d38ae51ec9cdb46f00ed9eda7ea4db17bc2c1450d58babaa9`

[Matching source](https://raw.githubusercontent.com/beckerd/psp_jgr_portable/test_builds/JGR_PORTABLE-1.0.2-source.zip) · [Checksums](SHA256SUMS.txt)
