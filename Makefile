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
CHIAKI_RPCRYPT := third_party/chiaki/src/rpcrypt.c
CFLAGS := -Wall -Werror -Wno-trigraphs -g
PAIR_CFLAGS := -DCHIAKI_LIB_ENABLE_MBEDTLS -Ithird_party/chiaki/include -Ithird_party/chiaki_compat -Wno-unused-variable
LDADD := -lkernel_sys -lSceRemoteplay -lSceSystemService -lSceRegMgr

all: $(ELF) $(ASTROKILL) $(ASTROLOCK)

$(CHIAKI_RPCRYPT): scripts/fetch_chiaki_rpcrypt.sh
	sh scripts/fetch_chiaki_rpcrypt.sh

vendor-pairing: $(CHIAKI_RPCRYPT)

main_v12_embed.c: main_v12.c
	sed -e 's/^int main(void)$$/int astro_v12_legacy_main(void)/' -e '/add_service("websrv","WebSrv","websrv\.elf",8080);/a\  add_service("remote","Astro Remote Worker","astrorem",45822);' $< > $@

main_v13_embed.c: main_v13.c main_v12_embed.c
	sed 's/^int main(void)$$/int astro_v13_legacy_main(void)/' main_v13.c > $@

main_v132_embed.c: main_v132.c main_v13_embed.c main_v12_embed.c
	sed 's/^int main(void)$$/int astro_v132_legacy_main(void)/' main_v132.c > $@

main_v137_build.c: main_v136.c main_v132_embed.c main_v13_embed.c main_v12_embed.c scripts/build_main_v137.py
	python3 scripts/build_main_v137.py main_v136.c $@

remote_service_safe.c: remote_service.c scripts/build_safe_remote_service.py
	python3 scripts/build_safe_remote_service.py remote_service.c $@

$(ELF): $(CHIAKI_RPCRYPT) main_v137_build.c remote_service_safe.c remote_service.h remote_worker.c remote_worker.h remote_ps5_source.c remote_ps5_source.h remote_pairing.c remote_pairing.h remote_page_v137.h astro_pair_client.c astro_pair_client.h astro_pair_crypto.c astro_pair_crypto.h chiaki_mbedtls_compat.c main_v132_embed.c main_v13_embed.c main_v12_embed.c main_v03.c astro_process_name.c
	$(CC) $(CFLAGS) $(PAIR_CFLAGS) -o $@ main_v137_build.c remote_service_safe.c remote_worker.c remote_ps5_source.c remote_pairing.c astro_pair_client.c astro_pair_crypto.c chiaki_mbedtls_compat.c $(CHIAKI_RPCRYPT) astro_process_name.c $(LDADD)

$(ASTROKILL): astrokill.c
	$(CC) $(CFLAGS) -o $@ astrokill.c

$(ASTROLOCK): astrolock_web.c astrolock_offact.c astrolock_offact.h astrolock_process_name.c
	$(CC) $(CFLAGS) -o $@ astrolock_web.c astrolock_offact.c astrolock_process_name.c -lSceRegMgr

clean:
	rm -f $(ELF) $(ASTROKILL) $(ASTROLOCK) main_v12_embed.c main_v13_embed.c main_v132_embed.c main_v136_embed.c main_v137_build.c remote_service_safe.c

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^
