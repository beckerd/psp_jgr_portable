# Third Party Notices

This repository contains native PSP infrastructure, reconstructed game and DSP
behavior, conversion metadata, and third-party components. Retained metadata
includes source filenames, addresses, counts and resource selectors. Original
game assets are supplied or generated locally from the user's disc.
`psp/src/source_inverse.h` preserves SH-4 arithmetic order;
`psp/src/source_data_reverb_program.inc` translates AICA DSP operations with
effect parameters loaded from prepared assets. Review manifests classify source
contents and validation scope; they do not determine independent authorship or
licensing. [LICENSE](LICENSE) sets out the terms for original project
contributions, reconstructed code and third-party components.

- [ICON0](icon/ICON0.png) and [PIC1](icon/PIC1.png) are XMB artwork
  created by the maintainer for this project.
- The PSP installer and gameplay module link Newlib 4.5.0 libc/libm; PSP
  executables and kernel helpers use PSPSDK libraries. The complete
  [Newlib notices](tools/portable/notices/Newlib-4.5.0.txt) and
  [PSPSDK license](tools/portable/notices/PSPSDK.txt) are copied from the
  build toolchain's `share/licenses/newlib/COPYING.NEWLIB` and
  `share/licenses/pspsdk/LICENSE`. They accompany PSP binaries in the runtime
  notice bundle and are retained in the matching source archive.
- The PSPSDK exception handler attribution and license must accompany builds:
  [psp/crash_handler_license.txt](psp/crash_handler_license.txt).
- Save metadata integrity hashing in `psp/src/savedata.c` adapts the
  [PSPSDK savedata sample](https://github.com/pspdev/pspsdk/blob/master/src/samples/savedata/encrypt/hash.c),
  copyright (c) 2005 Jim Paris and psp123, under BSD-3-Clause. Its
  [notice](tools/portable/notices/PSPSDK-savedata.txt) accompanies PSP binaries
  and the matching source archive.
- The Media Engine bootstrap in `psp/tools/meprx/me_stub.S` adapts the boot
  setup from [psp-media-engine-custom-core v2.0.1](https://github.com/mcidclan/psp-media-engine-custom-core/tree/a49526f3821f0c85c089a38a61e81ff3a453174d),
  copyright (c) 2025 m-c/d (mcidclan), under MIT. Its
  [license](tools/portable/notices/libmecore-2.0.1.txt) accompanies PSP binaries
  and the matching source archive. The stub documents the hardware references
  used for its cache initialization; the full libmecore library is not linked.
- The first-launch indexed texture converter includes an adapted Pillow 11.3.0
  octree quantizer and a FreeBSD sorting routine. The source retains their
  copyright notices. Their licenses accompany source and binary distributions:
  [Pillow notices](tools/portable/notices/Pillow-11.3.0.txt) and
  [FreeBSD sort notice](tools/portable/notices/FreeBSD-sort.txt).
- The native installer includes Flycast/reicast SH-4 and AICA hardware tables
  in `portable_hardware_fsca.inc` and `portable_hardware_aica.inc`, licensed
  under GPL-2.0-or-later. The tested revision is
  `149e651d24b3e3b3ddeff1d022f47813c40e9fc5` of
  [Flycast](https://github.com/flyinghead/flycast); upstream hashes are recorded
  in `tools/portable/reference_sources.json`. The
  [GPL v2 license](tools/portable/notices/Flycast-GPL-2.0.txt) and
  [hardware-source inventory](tools/portable/notices/Flycast-hardware.txt)
  identify the terms, unchanged table and excerpt. Their combined-program
  scope is described below.
- [flycast-fast-forward.patch](tools/portable/patches/flycast-fast-forward.patch)
  adds a Lua fast-forward control for local reference captures. It is a patch
  to Flycast distributed under GPL-2.0-or-later; the
  [GPL v2 license](tools/portable/notices/Flycast-GPL-2.0.txt) accompanies it.
- The browser preparer includes compatible Newlib math routines and
  Emscripten/Musl runtime code, as well as shared converter components described
  above. Their component notices are displayed in the page's **Licenses &
  source** disclosure and retained in its matching source archive.
  [Browser notices](tools/portable/browser/THIRD_PARTY_NOTICES.txt),
  [math provenance](tools/portable/browser/math-source.json) and
  [toolchain provenance](tools/portable/browser/third-party-source.json)
  identify these inputs. Preserve each component's notices and applicable terms.
- External PSPSDK, Flycast, Pillow and NumPy build/conversion dependencies
  retain their own licenses. Their tool executables are not bundled.

## GPL build scope

The GPL tables are compiled through `psp/src/portable_hardware.c` into both
conversion programs. GPL scope is not confined to the two `.inc` files:

| Build component | Scope |
| --- | --- |
| PSP installer (`.setup/jgrsetup.elf`, packaged as EBOOT's `DATA.PSP`) | One combined program containing the GPL tables, converter, native replay and linked game routines. |
| Browser WebAssembly converter (embedded in `prepare.html`) | One combined program built from the shared installer sources and browser support code. |
| Gameplay module (`jgrview.prx`, bundled as `game.prx`) and helper PRXs | Separate modules; the gameplay build does not directly compile the GPL tables. Packaging alone does not put these modules under the GPL. |

The build boundaries are defined by `psp/Makefile`,
`tools/portable/build_device_setup.py` and `tools/portable/browser/build.py`.
Each combined conversion program must meet the GPL's whole-program terms when
distributed. Corresponding source includes all of its linked modules, headers
and build scripts, not just the tables. Include the exact matching source ZIP
with both `EBOOT.PBP` and `prepare.html`, even when distributing either alone.

The generated `source_data_player_fsca.jsd` copies the GPL-covered FSCA table;
`source_data_smsb_tables.jsd` combines derived AICA timing tables with MANATEE
pitch data read from the user's disc. Local generation does not remove the
underlying rights or license the disc-derived data under the GPL or MIT.
