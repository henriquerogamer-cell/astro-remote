# Astro v3 - standalone PS5 administration payload

PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := astro_remote.elf
SRC := astro_v3.c
DEPS := astro_part1.inc astro_part2.inc astro_v3_core.inc astro_v3_ui.inc astro_v3_profile_core.inc astro_v3_profile_ui.inc astro_v3_router_profile.inc

CFLAGS := -Wall -Wextra -O2 -g -lSceUserService
LDADD :=

all: $(ELF)

$(ELF): $(SRC) $(DEPS)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDADD)

clean:
	rm -f $(ELF)

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^
