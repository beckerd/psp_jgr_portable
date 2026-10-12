# Build and asset preparation

This repository builds the PSP application and an optional browser asset
preparer. Both use the same C converters and versioned asset identities.
Building either artifact requires no game image, extracted assets or emulator.

For installation, start with the [project README](../../README.md).
The [browser guide](browser/README.md) explains the desktop preparation flow.

## Repository and releases

The Git repository contains source, build tools, documentation and packaging
inputs. Build a checkout or GitHub's automatic source download using the steps
below. Compiled packages are distributed through GitHub Releases.

A game release ZIP contains only `README.md`, `Prepare 1.0.html`, `LICENSES.txt`
and `EBOOT.PBP`. Use GitHub's automatic source downloads for the matching release tag; do not
upload another source archive. Users still prepare the game assets from their own supported disc
image before playing. Keep local builds and release packages under ignored
`build/`; publish packages as release attachments.

## Build requirements

- Python **3.10 or newer**.
- Git, GNU Make and a PSP toolchain on `PATH`. The checked toolchain uses PSP GCC
  **15.2.0**, PSPSDK and its PSP libraries and packaging tools.
- Emscripten **6.0.12**, only when building `prepare.html`. The browser build
  deliberately rejects other versions.
- A C compiler for host C tests; Node **22+** and desktop Google Chrome for
  browser tests.

Compiling the PSP executable and browser converter uses Python's standard
library. Packaging a release with the project's ICON0 and PIC1 images requires
Pillow **11.3.0**:

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install Pillow==11.3.0
```

Optional host conversion and tests also use NumPy. Install their pinned
dependencies with `python3 -m pip install -r requirements-portable.txt`;
see [requirements-portable.txt](../../requirements-portable.txt).

Set `PSPDEV` to the root of your installed PSP toolchain and put its tools
on `PATH` (substitute your installation path):

```sh
export PSPDEV=/path/to/pspdev
export PATH="$PSPDEV/bin:$PATH"
```

This must make `psp-config`, `psp-gcc`, `psp-nm`, `psp-prxgen` and
the other SDK tools available. Install and activate Emscripten 6.0.12 separately.
Ensure `python3 --version` reports at least 3.10 on that same `PATH`: Emscripten also starts Python subprocesses, so
invoking the first command through a newer Python alone is insufficient.
Toolchains are not part of this repository.

Run the commands below from the repository root. On systems where GNU Make is
named `make`, substitute it for `gmake`. Use fresh packaging and test output
directories.

## Build the PSP executable

```sh
gmake -C psp -j4
python3 tools/portable/source_check.py --out build/reviewed-source \
  --identity psp/src/portable_asset_identity.h
python3 tools/portable/audit_build.py --build psp \
  --nm psp-nm --out build/build-audit.json
python3 tools/portable/package.py starter --build psp \
  --native-identity tools/portable/native_replay_identity.json \
  --icon0 icon/ICON0.png --pic1 icon/PIC1.png \
  --out build/release/JGR_PORTABLE
```

The build creates an internal, artwork-free `psp/EBOOT.PBP`. Packaging adds
the project's `icon/ICON0.png` and `icon/PIC1.png` to the release copy
in `build/release/JGR_PORTABLE/`, preserving its executable and bundled-module
bytes. The EBOOT includes the game, on-device installer, project helper modules,
conversion instructions and required runtime notices. No separate PRX files
or disc-extracted game assets belong in the public release folder.

Packaging requires a new destination and refuses to overwrite an existing one.
For another build, choose a fresh output path rather than an installed game
folder. It does not modify saves or prepared installations.

The default build has release diagnostics disabled. Holding Select for three
seconds still toggles the runtime overlay. Leave `DIAGNOSTICS=0`,
`DEVICE_SETUP=1` and `INSTALLER_DEVELOPMENT=0` for a release. Incomplete
converter recipes are rejected. Do not enable `JGR_TEST_FIXTURES` in releases.

The source check rejects changed reviewed inputs and unreviewed files that
could be added to Git, including already tracked files that are now ignored.
It verifies the generated identity against checksum metadata and keeps a fresh
source-only snapshot in `build/reviewed-source`; that destination must not
already exist.
Changes to the reviewed source need a corresponding source review before this
check will pass. Its generated receipts bind the checked files.

The compiled-data audit checks that external game-data storage is
zero-initialized and that compiler dependencies do not include excluded
original data. It also binds the audited game, installer and helpers to the
packaged executable. Run it after building and before changing the sources.

## Build the browser preparer

Build the PSP executable first: that generates the asset identity shared with
the browser converter. Then run:

```sh
python3 tools/portable/browser/build.py \
  --emcc /path/to/emsdk/upstream/emscripten/emcc.py \
  --source-url https://github.com/beckerd/psp_jgr_portable/archive/refs/tags/1.0.zip \
  --psp psp --out build/browser/preparer
cp build/browser/preparer/prepare.html \
  "build/release/JGR_PORTABLE/Prepare 1.0.html"
```

The game folder now contains `EBOOT.PBP` and `Prepare 1.0.html`. Add the
installation `README.md` and complete `LICENSES.txt`, and ZIP only those four
files. Keep any checksum file outside the game ZIP.

Set `--source-url` to GitHub's automatic source download for the release tag
that contains the exact source used to build both converters. Do not upload a
duplicate source attachment. Use a fresh browser output directory for each
release. The preparer's **Licenses & source** disclosure links to that tag's
source and includes the full project and component notices without JavaScript.

Distribute the emitted HTML, not `tools/portable/browser/prepare.html`, which
is a source template. The HTML includes its worker and WebAssembly converter;
its user does not need Python, a compiler or a server.

For an offline distribution, omit `--source-url`. The build then emits a
local matching source ZIP with a checksum, which must stay beside the HTML.
The source snapshot excludes Git history, SDKs, compiled outputs and game data.
Source is not needed on the PSP.

The [project license](../../LICENSE) describes MIT coverage.
[Third-party notices](../../THIRD_PARTY_NOTICES.md#gpl-build-scope) describe the
GPL scope of both converters.

Keep the HTML and EBOOT from the same build together. The preparer checks the
selected executable's asset identity and instructions before conversion.
Browser compilation writes a `build.json` receipt recording the sources,
compiler and artifact hashes; a successful build alone is not a conversion
or gameplay test.

## What is generated

The source catalogs describe filenames, expected sizes and checksums. Recipes
retain source addresses, counts, individual resource selectors and conversion
schemas describing how to read the user's supported disc. Meshes, textures,
audio, captured routes and decoded asset lookup payloads are generated locally.
For example, `chars_spec.txt` uses `texbinding=` to read texture mappings
from source data, or scalar `tex=` selectors, rather than decoded map arrays.

The default `native-v1` profile generates routes using the native player,
collision and animation implementations. Its identity also fingerprints the
generator source. A change to that source requires reviewed output validation;
do not bypass a stale fingerprint by replacing expected hashes without testing.

The build generates `psp/src/portable_asset_identity.h`, `install.recipe`
and crash/build identifiers from source and metadata. These are build outputs.
Runtime loaders leave game-data storage empty until they read the user's
prepared files.

The installed layout separates game files from saves. The `data/` paths
below are inside the game folder; the save directory is outside it.

| Directory | Contents |
| --- | --- |
| `data/assets/` | Prepared game assets |
| `ms0:/PSP/SAVEDATA/JGR_PORTABLE/` | Player saves and backups on the Memory Stick |
| `data/cache/` | Installation marker, bundled modules and startup cache |
| `data/setup/` | Optional locally prepared startup/setup artwork |
| `data/log/` | Setup and runtime reports |

A launch from internal storage uses `ef0:/PSP/SAVEDATA/JGR_PORTABLE/` instead.
The save files are `jgr_story.sav` and its `.bak` recovery copy; a `.tmp`
file is used while writing. Back up the save directory on the same storage
device as the game. Host and browser conversion tools do not create or manage
save directories.

The optional `data/setup/startup-warning.jbg` comes from the user's disc. It
preserves the original 4:3 image, centered at 360×270 on a black 480×272 screen.
Missing or invalid artwork does not prevent launch. It is not embedded in the
executable, HTML or source repository. ICON0 and PIC1 are XMB artwork created
by the maintainer for this project. They are included under `icon/` and added
during release packaging.
The browser can derive the setup background from the packaged PIC1 image;
the original startup warning still comes from the user's disc.

Later launches trust a matching completed-installation marker and do not scan
the entire asset pack or reopen the disc. An explicit repair invalidates the
marker before changing installed assets.

## Tests

Install the optional Python requirements before tests that use Pillow or NumPy.
These focused checks use synthetic inputs and do not need a game image:

```sh
python3 -m unittest discover -s tools/portable -p 'test_bundle.py'
python3 -m unittest discover -s tools/portable -p 'test_package.py'
python3 -m unittest discover -s tools/portable -p 'test_startup_warning.py'
python3 -m unittest discover -s tools/portable/browser/tests -p 'test_*.py'
```

The browser filesystem tests use an isolated Chrome profile and temporary
browser storage:

```sh
node tools/portable/browser/test_filesystem.mjs \
  --chrome /path/to/chrome --out build/browser/filesystem-test
```

See the [browser guide](browser/README.md#validation) for full conversion
comparison using an explicitly supplied disc and expected installation.
Before releasing converter changes, compare every generated asset, exercise
the external-folder browser flow, and test startup and gameplay on the PSP.
Emulator timing is not a measurement of physical PSP performance.

## Contributor boundaries

Keep disc images, extracted or prepared assets, captures, saves and build outputs
outside source control. Use ignored `build/` or `private/` directories for
local work. The ignore rules reduce accidental additions; they do not replace
review of the files being committed or packaged.

Preserve notices in individual files and follow the component requirements in
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).

