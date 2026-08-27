PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := astro_remote.elf
ASTROKILL := astrokill.elf
ASTROLOCK := astrolock.elf
CFLAGS := -Wall -Werror -Wno-trigraphs -g
LDADD := -lkernel_sys -lSceRemoteplay -lSceSystemService

all: $(ELF) $(ASTROKILL) $(ASTROLOCK)

main_v12_embed.c: main_v12.c
	sed 's/^int main(void)$$/int astro_v12_legacy_main(void)/' $< > $@

main_v13_embed.c: main_v13.c main_v12_embed.c
	sed 's/^int main(void)$$/int astro_v13_legacy_main(void)/' main_v13.c > $@

main_v132_embed.c: main_v132.c main_v13_embed.c
	sed 's/^int main(void)$$/int astro_v132_legacy_main(void)/' main_v132.c > $@

$(ELF): main_v136.c remote_service.c remote_service.h remote_worker.c remote_worker.h remote_ps5_source.c remote_ps5_source.h remote_pairing.c remote_pairing.h remote_page_v137.h main_v132_embed.c main_v13_embed.c main_v12_embed.c main_v03.c astro_process_name.c
	$(CC) $(CFLAGS) -o $@ main_v136.c remote_service.c remote_worker.c remote_ps5_source.c remote_pairing.c astro_process_name.c $(LDADD)

$(ASTROKILL): astrokill.c
	$(CC) $(CFLAGS) -o $@ astrokill.c

$(ASTROLOCK): astrolock_web.c astrolock_offact.c astrolock_offact.h astrolock_process_name.c
	$(CC) $(CFLAGS) -o $@ astrolock_web.c astrolock_offact.c astrolock_process_name.c -lSceRegMgr

clean:
	rm -f $(ELF) $(ASTROKILL) $(ASTROLOCK) main_v12_embed.c main_v13_embed.c main_v132_embed.c main_v136_embed.c main_v137_build.c

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^
