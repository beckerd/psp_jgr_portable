# Offline asset preparer

This preparer runs the shared C asset converter in a browser using
WebAssembly. The release's `Prepare 1.0.html` contains its own code and tools. It needs no
server, account, Python installation, or upload of the game image.

The converter uses the same asset formats and checksums as the PSP setup path.
Browser conversion, complete output comparison, and PSP startup are separate
validation steps; a successful build alone does not establish compatibility.

## Preparing a release on your computer

1. Download and extract a compiled package from this repository's GitHub
   Releases page. Use its matching `Prepare 1.0.html` and `EBOOT.PBP`.
2. Place your supported GDI image and all its track files beside `EBOOT.PBP`.
   Keep the track filenames unchanged. The folder should contain one GDI image.
3. Open `Prepare 1.0.html` in desktop Google Chrome. Select the release folder and
   allow the requested read/write access. The page checks for `EBOOT.PBP`, one
   GDI, and every track named by that GDI. Missing files are listed before
   preparation can start in an always-visible checklist above the action button,
   with a checkmark for each file found and an X for each missing file. After
   adding missing files, choose **Try again** to recheck the same folder.
   A successfully checked folder is remembered for this
   preparer's location and restored on later visits. Chrome may ask for access
   again when you choose **Prepare Assets**; **Change folder** remains available.
4. Choose **Prepare Assets** and keep the page open until it reports success.
5. Copy `EBOOT.PBP` and the complete generated `data` folder together into a game
   folder under `PSP/GAME/` on the memory card, then safely eject the card.

The original GDI and track files stay in place. They are not needed on the PSP
after preparation succeeds. `Prepare 1.0.html` is also only needed on the computer.
Cancellation waits for the converter to stop; keep the page open while it does.

## Licenses and source

Open **Licenses & source** at the bottom of the page to read the project license
and full component notices and obtain the matching source archive. The disclosure
works without JavaScript and remains available in browsers that cannot prepare assets.
Use Tab to focus its summary, then Enter or Space to open it.

The page links to GitHub's automatic **Source code (zip)** download for the
matching release tag. That tag contains the source, build instructions, project
license and component notices. Do not upload a duplicate source archive.
See [THIRD_PARTY_NOTICES.md](../../../THIRD_PARTY_NOTICES.md) for provenance
and GPL build scope.

Source is not needed to prepare assets or play. Asset preparation still works
offline; downloading source from GitHub needs internet access. Preserve the
project license and component notices.

For an offline distribution, build without `--source-url` and keep the emitted
matching source ZIP beside the HTML so its local source link works.

## Prepared files and saves

PSP saves live outside the game folder, in
`ms0:/PSP/SAVEDATA/JGR_PORTABLE/`, or
`ef0:/PSP/SAVEDATA/JGR_PORTABLE/` when launched from internal storage.
Back up that directory to your computer. The browser preparer works within
the selected release folder; it does not manage the PSP's separate save directory.

Preparation also creates `data/setup/startup-warning.jbg` from the original
graffiti warning in your own game image. Normal startup shows that image without
added text or a forced delay. Its original 4:3 proportions are preserved: a
360×270 image is centered on the black 480×272 screen. Normal PSP launches
never reopen the game image, and missing or damaged artwork does not block play.

The documented release package includes the project's PIC1 background.
When the selected EBOOT contains a PIC1 image, the browser decodes that image and
creates `data/setup/background.jbg` for the PSP asset-preparation screen. The image must be
a single 480×272 PNG; its output uses the same RGB565 format and checksum as the
PSP setup artwork loader. Missing PIC1 is supported and leaves the plain setup
screen available. Invalid image data is rejected before asset conversion, and a
failed write never replaces an existing background with a partial file. No PNG
or ROM artwork is embedded in the preparer HTML.

## Browser support and current limits

- The preparer requires desktop Google Chrome and the File System Access API.
  Complete conversion has been tested on macOS (Apple silicon) with Chrome
  154.0.8037.98. Chrome on Windows and Linux is expected to work, but conversion
  and folder-access testing on those systems is still pending. Other detected
  browsers show a Chrome requirement screen.
- The requirement screen offers a best-effort **Copy address & launch Chrome**
  button and a smaller download link. Recent desktop Chrome registers the
  `google-chrome:` scheme, but does not accept local file URLs through it. The
  button copies the page address and opens a blank Chrome page; paste the address
  into Chrome's address bar and press Enter. A separate **Copy address** button
  and selectable address remain available if automatic copying is blocked.
  You can also drag `Prepare 1.0.html` into Chrome. If the launch link is unsupported,
  use **Open with → Google Chrome** on the HTML file.
  See Chromium's [launch URL validation](https://raw.githubusercontent.com/chromium/chromium/main/chrome/browser/ui/startup/url_util.cc).
- Opening an HTML file does not grant access to its neighboring files. Chrome
  still requires the user to choose and authorize the release folder before
  the preparer can inspect or write it, following Chrome's
  [directory permission flow](https://developer.chrome.com/docs/capabilities/web-apis/file-system-access#opening_a_directory_and_enumerating_its_contents).
  The first folder selection cannot be inferred from the HTML's path. Subsequent
  visits can restore the previously selected handle from IndexedDB, without
  granting permission automatically. If browser storage is unavailable or
  cleared, select the folder again.
- The page cannot determine free space on the selected disk. Insufficient space
  is reported when a write or close fails.
- The browser needs memory for conversion. Source tracks are read in pieces;
  the entire game image is not copied into WebAssembly memory.
- The converter preserves the PSP's output checks, including the exact replay
  output identities. It does not promise a completion time or replace a failed
  output check with an approximation.
- Do not edit the selected folder or its source tracks during preparation. The
  source image must stay available until preparation finishes.

## Building

Use Python **3.10+** and Emscripten **6.0.12**. Check that `python3` on
`PATH` is also at least 3.10, because compiler subprocesses invoke it. The
person using the completed HTML needs neither tool.

Run the [PSP build and packaging steps](../README.md#build-the-psp-executable)
from this repository first. They generate the matching
`psp/src/portable_asset_identity.h`. Then, from the repository root:

```sh
python3 tools/portable/browser/build.py \
  --emcc /path/to/emsdk/upstream/emscripten/emcc.py \
  --source-url https://github.com/beckerd/psp_jgr_portable/archive/refs/tags/1.0.zip \
  --psp psp --out build/browser/preparer
cp build/browser/preparer/prepare.html \
  "build/release/JGR_PORTABLE/Prepare 1.0.html"
```

The build emits `prepare.html` linked to GitHub's existing source download for
release tag `1.0`; change the URL for another release and ensure that tag contains
the exact source used for both converters. With `--source-url`, no additional
source ZIP is emitted. Package only `README.md`, `Prepare 1.0.html`,
`LICENSES.txt` and the matching `EBOOT.PBP` inside the game ZIP. Use a fresh
browser output directory for each release. The source `prepare.html` in this
directory is a template, not the working preparer.
Do not include compiler objects, test profiles or generated game assets in
a release. No disc image or EBOOT is read when compiling the browser converter;
the user supplies those at preparation time.

The browser-only controller math uses compatible Newlib float routines so
recorded inputs retain the PSP's rounding. Only the controller decoder is
redirected; the PSP build is unchanged. `math-source.json` records the pinned
upstream source and hashes. The emitted HTML displays the complete Newlib,
Emscripten, Musl and shared-converter component notices in its disclosure.
The release tag preserves those notices and their source files.
See the [GPL build scope](../../../THIRD_PARTY_NOTICES.md#gpl-build-scope)
for the licensing requirements of both converters.

## Validation

The browser build emits `build.json` with compiler, source, identity,
source snapshot and artifact hashes, and the source download URL. Its initial status is
`built_not_yet_validated`: compilation does not prove complete conversion
or PSP compatibility.

Synthetic browser checks cover file operations, cancellation/error cleanup,
background image conversion, malformed inputs and interrupted writes. They need
Node 22+ and Chrome, but no game image:

```sh
node tools/portable/browser/test_filesystem.mjs \
  --chrome /path/to/chrome --out build/browser/filesystem-test
python3 -m unittest discover -s tools/portable/browser/tests -p 'test_*.py'
```

Full conversion testing requires a privately supplied supported GDI and an
accepted prepared installation:

```sh
node tools/portable/browser/test_browser.mjs \
  --html build/browser/preparer/prepare.html \
  --eboot build/release/JGR_PORTABLE/EBOOT.PBP \
  --gdi /path/to/game/image.gdi \
  --expected /path/to/accepted/data/assets \
  --save-sentinel yes \
  --out build/browser/conversion-test
```

Set `CHROME` to the browser executable if it is not at the harness's default
macOS location. The harness runs the emitted production worker in an isolated
Chrome profile and prepares a fresh browser-private OPFS directory. It compares
every generated asset with the accepted installation, exports the result and
records unexpected network requests. The save sentinel checks that two synthetic
files in `PSP/SAVEDATA/JGR_PORTABLE/`, outside the test application's
`PSP/GAME/JGR_PORTABLE/` folder, survive preparation. Both directories live in
isolated browser storage. Use a fresh output directory for every run.

One complete automated run on the macOS/Chrome combination above took about
2 minutes 20 seconds using browser-private storage. That measurement excludes
exporting the results and copying them to a Memory Stick; preparation in a
selected release folder depends on that computer and storage.

This automated destination does not replace a manual test of the folder picker
and writes to a real release folder. Test that flow on each supported desktop OS,
then verify the prepared folder on a PSP. Check both first startup and a later
startup with the disc files removed, and confirm saves load correctly.
Do not infer physical PSP performance from emulator timing
or promise installation times from another machine's measurements.

The preparation worker receives the selected handle only after the user grants
read/write permission. Chrome continues to enforce those permissions.
Keep the source tracks available and leave the selected folder untouched until
the worker finishes or cancellation completes.

Keep generated output, private source-image paths and browser test profiles
under ignored `build/` or outside the source tree. Conversion assets are for
the user's installation, not for publication with the source or release tools.
