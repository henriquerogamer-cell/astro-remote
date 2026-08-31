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
DEPS := astro_part1.inc astro_part2.inc astro_v3_core.inc astro_v3_profile_core.inc astro_v3_beta_core.inc astro_v3_webdetect.inc astro_v3_webdetect_plus.inc astro_v3_webdetect_runtime.inc astro_v3_beta_ui.inc astro_v3_auth_ui.inc astro_v3_files_explorer.inc astro_v3_payload_polish.inc astro_v3_payload_webcard.inc astro_v3_dashboard_runtime.inc astro_v3_profile_ui.inc astro_v3_proxy_api_compat.inc astro_v3_beta_router.inc

CFLAGS := -Wall -Wextra -O2 -g -lSceUserService
LDADD :=

all: $(ELF)

$(ELF): $(SRC) $(DEPS)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDADD)

clean:
	rm -f $(ELF)

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^
