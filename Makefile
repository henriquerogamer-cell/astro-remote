# Astro v3 beta - standalone PS5 administration payload

PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := astro_remote.elf
SRC := astro_v3.c
DEPS := astro_part1.inc astro_part2.inc astro_v3_identity.inc astro_v3_core.inc astro_v3_profile_core.inc astro_v3_beta_core.inc astro_v3_webdetect.inc astro_v3_webdetect_plus.inc astro_v3_webdetect_runtime.inc astro_v3_beta_ui.inc astro_v3_shadowmount_ui.inc astro_v3_process_control.inc astro_v3_auth_ui.inc astro_v3_homebrew_tls.inc astro_v3_payload_catalog.inc astro_v3_catalog_clickfix.inc astro_v3_source_releases.inc astro_v3_homebrew.inc astro_v3_homebrew_web.inc astro_v3_pkg_installer.inc astro_v3_launcher.inc assets/astro_launcher.png astro_v3_files_jsfix.inc astro_v3_files_explorer.inc astro_v3_saves_cards.inc astro_v3_saves_native.inc astro_v3_saves_native_v2.inc astro_v3_saves_zip.inc astro_v3_payload_polish.inc astro_v3_payload_webcard.inc astro_v3_dashboard_runtime.inc astro_v3_profile_ui.inc astro_v3_proxy_api_compat.inc astro_v3_beta_router.inc

CFLAGS := -Wall -Wextra -O2 -g -D_BSD_SOURCE -std=gnu11
CFLAGS += `$(PS5_PAYLOAD_SDK)/bin/prospero-curl-config --cflags`
LDADD := -lsqlite3 -lkernel_sys -lkernel -lSceIpmi -lSceAppInstUtil -lSceSystemService -lSceUserService -lSceFsInternalForVsh -lSceRegMgr
LDADD += `$(PS5_PAYLOAD_SDK)/bin/prospero-curl-config --libs`

all: $(ELF)

$(ELF): $(SRC) $(DEPS)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDADD)

clean:
	rm -f $(ELF)

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^
