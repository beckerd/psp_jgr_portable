# Source-only export build. Asset identity comes from checksum metadata.
# Release audits check specific data boundaries: inventoried external data symbols
# must use BSS storage, and excluded generated includes must not be dependencies.
# The audits also verify source/module bindings.
TARGET = jgrview
.DEFAULT_GOAL := all
GAME_SOURCES = $(filter-out $(wildcard src/portable_*.c),$(wildcard src/*.c)) src/portable_assets.c src/portable_bundle.c src/portable_startup_cache.c
OBJS = $(patsubst %.c,%.o,$(GAME_SOURCES))
OBJS += src/savedata_imports.o
DIAGNOSTICS ?= 0
DEVICE_SETUP ?= 1
# Incomplete installer plans are permitted only for local converter development.
INSTALLER_DEVELOPMENT ?= 0
REPLAY_PROFILE ?= native-v1
NATIVE_REPLAY_IDENTITY ?= ../tools/portable/native_replay_identity.json
REPLAY_PROFILE_ARGS = --replay-profile $(REPLAY_PROFILE) $(if $(filter native-v1,$(REPLAY_PROFILE)),--native-identity "$(NATIVE_REPLAY_IDENTITY)")
REPLAY_PROFILE_INPUTS = ../tools/portable/replay_identity.py $(if $(filter native-v1,$(REPLAY_PROFILE)),$(NATIVE_REPLAY_IDENTITY))
CFLAGS = -O2 -G0 -Wall -std=gnu99 -ffp-contract=off -g
CFLAGS += -fstack-usage -Werror=frame-larger-than=24576 -MMD -MP
CFLAGS += -DJGR_DIAGNOSTICS=$(DIAGNOSTICS) -DJGR_PORTABLE_ASSETS=1
CFLAGS += -DJGR_DEVICE_SETUP=0 -DJGR_BUNDLED_RUNTIME=$(DEVICE_SETUP)
CFLAGS += -include src/portable_assets.h $(JGR_EXTRA_CFLAGS)
src/savedata_imports.o: src/savedata_imports.S
	$(CC) $(filter-out -include src/portable_assets.h,$(CFLAGS)) -c $< -o $@
src/main.o: CFLAGS += -Werror=frame-larger-than=12288
src/audio_job.o src/reverb_job.o: CFLAGS += -fno-builtin
LDFLAGS += -Wl,--wrap=sceGeListUpdateStallAddr,--wrap=sceGuGetMemory
LIBS = -lpspgum -lpspgu -lpspaudiolib -lpspaudio -lpsppower -lm
BUILD_PRX = 1
PSP_FW_VERSION = 660
PSP_LARGE_MEMORY = 0
EXTRA_TARGETS = EBOOT.PBP
SFOFLAGS = -s DISC_ID=JGRV00001
PSP_EBOOT_TITLE = JGR Portable
ifeq ($(DEVICE_SETUP),1)
PSP_EBOOT = game-preview.PBP
install.recipe: ../tools/portable/install_plan.py ../tools/portable/asset_catalog.json ../tools/portable/raw_assets.json ../tools/portable/mission_sets.json ../tools/portable/tables_recipes.py ../tools/portable/resource_recipes.py ../tools/portable/misc_recipes.py ../tools/portable/model_recipes.py ../tools/portable/model_jobs.json ../tools/portable/world_recipes.py ../tools/portable/scene_recipes.py ../tools/portable/graffiti_recipes.py ../tools/portable/replay_recipes.py ../tools/portable/capture_jobs.json $(wildcard src/portable_tables*.inc) .portable-config
	python3 ../tools/portable/install_plan.py --out $@ --report install-coverage.json --replace $(REPLAY_PROFILE_ARGS) $(if $(filter 1,$(INSTALLER_DEVELOPMENT)),--development,)
runtime-notices.txt: ../LICENSE ../THIRD_PARTY_NOTICES.md crash_handler_license.txt ../tools/portable/notices/Pillow-11.3.0.txt ../tools/portable/notices/FreeBSD-sort.txt ../tools/portable/notices/Flycast-hardware.txt ../tools/portable/notices/Flycast-GPL-2.0.txt ../tools/portable/notices/Newlib-4.5.0.txt ../tools/portable/notices/PSPSDK.txt ../tools/portable/notices/PSPSDK-savedata.txt ../tools/portable/notices/libmecore-2.0.1.txt
	cat $^ > $@
portable_helpers.psar: jgrview.prx jgr_me.prx jgr_crash.prx runtime-notices.txt install.recipe ../tools/portable/bundle.py
	python3 ../tools/portable/bundle.py --me jgr_me.prx --crash jgr_crash.prx --notice runtime-notices.txt --recipe install.recipe --game jgrview.prx --out $@
EBOOT.PBP: portable_helpers.psar ../tools/portable/build_device_setup.py $(wildcard src/portable_*.c src/portable_*.h src/portable_*.inc) src/source_fmac.h src/source_float_fast.h src/stage_texture_usage.inc .portable-config $(PSP_EBOOT_ICON) $(PSP_EBOOT_PIC1)
	python3 ../tools/portable/build_device_setup.py --psp . --bundle portable_helpers.psar --out $@ $(if $(filter-out NULL,$(PSP_EBOOT_ICON)),--icon0 "$(PSP_EBOOT_ICON)") $(if $(filter-out NULL,$(PSP_EBOOT_PIC1)),--pic1 "$(PSP_EBOOT_PIC1)")
endif
# Leave XMB artwork unset until separately reviewed project artwork is added.
PSPSDK = $(shell psp-config --pspsdk-path)
$(OBJS): Makefile src/portable_asset_identity.h .portable-config
src/portable_asset_identity.h: ../tools/portable/package.py ../tools/portable/asset_catalog.json $(REPLAY_PROFILE_INPUTS) .portable-config
	python3 ../tools/portable/package.py identity --catalog ../tools/portable/asset_catalog.json --out $@ $(REPLAY_PROFILE_ARGS)
install.recipe: ../tools/portable/package.py $(REPLAY_PROFILE_INPUTS)
.PHONY: portable_config_force
.portable-config: portable_config_force
	@printf '%s\n' '$(CFLAGS) $(INSTALLER_DEVELOPMENT) $(REPLAY_PROFILE) $(NATIVE_REPLAY_IDENTITY) $(PSP_EBOOT_ICON) $(PSP_EBOOT_PIC1)' | cmp -s - $@ || printf '%s\n' '$(CFLAGS) $(INSTALLER_DEVELOPMENT) $(REPLAY_PROFILE) $(NATIVE_REPLAY_IDENTITY) $(PSP_EBOOT_ICON) $(PSP_EBOOT_PIC1)' > $@
all: jgr_me.prx jgr_crash.prx
jgr_me.prx: tools/meprx/main.c tools/meprx/power.inc tools/meprx/me_stub.S tools/meprx/Makefile src/me_shared.h src/me_protocol.h
	$(MAKE) -C tools/meprx
	cp tools/meprx/jgr_me.prx $@
jgr_crash.prx: tools/crashprx/main.c tools/crashprx/exception.S tools/crashprx/Makefile src/crash_shared.h
	$(MAKE) -C tools/crashprx
	cp tools/crashprx/jgr_crash.prx $@
.PHONY: crash_build_force
src/crash_build.inc: crash_build_force
	python3 ../decompile/tools/crash_build_id.py "$(CFLAGS)"
src/crash.o: src/crash_build.inc
PARAM.SFO EBOOT.PBP: Makefile
include $(PSPSDK)/lib/build.mak
-include $(OBJS:.o=.d)

src/portable_stage_derived.o: src/portable_stage_derived.h src/portable_stage_visibility.inc src/stage_texture_usage.inc

src/portable_stage_props.o: src/portable_stage_props.h src/portable_stage.h src/portable_models.h

src/portable_tables_cached.o: src/portable_tables.h
src/portable_scene_convert.o: src/portable_scene_convert.h src/portable_disc.h
src/portable_graffiti_convert.o: src/portable_graffiti_convert.h src/portable_disc.h

src/portable_grind_convert.o: src/portable_grind_convert.h src/portable_stage.h
src/portable_roster_convert.o: src/portable_roster_convert.h src/portable_models.h
src/portable_bubble.o: src/portable_bubble.h src/portable_models.h

src/portable_mission_cast.o: src/portable_mission_cast.h src/portable_disc.h

src/portable_people_convert.o: src/portable_people_convert.h src/portable_disc.h
src/portable_replay_demo_table.o: src/portable_replay_demo_table.h src/portable_replay_demo_table_data.inc src/portable_tables.h
