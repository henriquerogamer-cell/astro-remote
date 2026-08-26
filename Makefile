# Copyright (C) 2023 John Törnblom
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := astro_remote.elf
ASTROKILL := astrokill.elf
CFLAGS := -Wall -Werror -g
LDADD := -lSceRegMgr

all: $(ELF) $(ASTROKILL)

main_v12_embed.c: main_v12.c
	sed 's/^int main(void)$$/int astro_v12_legacy_main(void)/' $< > $@

main_v13_embed.c: main_v13.c main_v12_embed.c
	sed 's/^int main(void)$$/int astro_v13_legacy_main(void)/' main_v13.c > $@

main_v132_embed.c: main_v132.c main_v13_embed.c
	sed 's/^int main(void)$$/int astro_v132_legacy_main(void)/' main_v132.c > $@

$(ELF): main_v136.c remote_service.c remote_service.h remote_worker.c remote_worker.h remote_ps5_source.c remote_ps5_source.h main_v132_embed.c main_v13_embed.c main_v12_embed.c main_v03.c astro_process_name.c
	$(CC) $(CFLAGS) -o $@ main_v136.c remote_service.c remote_worker.c remote_ps5_source.c astro_process_name.c $(LDADD)

$(ASTROKILL): astrokill.c
	$(CC) $(CFLAGS) -o $@ astrokill.c

clean:
	rm -f $(ELF) $(ASTROKILL) main_v12_embed.c main_v13_embed.c main_v132_embed.c

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^

debug: $(ELF)
	gdb-multiarch \
	-ex "set architecture i386:x86-64" \
	-ex "target extended-remote $(PS5_HOST):2159" \
	-ex "file $(ELF)" \
	-ex "remote put $(ELF) /data/$(ELF)" \
	-ex "set remote exec-file /data/$(ELF)" \
	-ex "start"
