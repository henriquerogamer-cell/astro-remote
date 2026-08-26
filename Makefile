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

main_v136_embed.c: main_v136.c main_v132_embed.c
	sed 's/^int main(void)$$/int astro_v136_legacy_main(void)/' main_v136.c > $@

main_v137_build.c: main_v137.c main_v136_embed.c
	{ printf '%s\n' '#include "main_v136_embed.c"'; tail -n +4 main_v137.c; } > $@

$(ELF): main_v137_build.c remote_service.c remote_service.h remote_worker.c remote_worker.h remote_ps5_source.c remote_ps5_source.h main_v136_embed.c main_v132_embed.c main_v13_embed.c main_v12_embed.c main_v03.c astro_process_name.c
	$(CC) $(CFLAGS) -o $@ main_v137_build.c remote_service.c remote_worker.c remote_ps5_source.c astro_process_name.c $(LDADD)

$(ASTROKILL): astrokill.c
	$(CC) $(CFLAGS) -o $@ astrokill.c

clean:
	rm -f $(ELF) $(ASTROKILL) main_v12_embed.c main_v13_embed.c main_v132_embed.c main_v136_embed.c main_v137_build.c

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^
