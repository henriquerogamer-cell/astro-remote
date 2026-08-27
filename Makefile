PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := astro_remote.elf
ASTROREM := astrorem.elf
ASTROKILL := astrokill.elf
ASTROLOCK := astrolock.elf
CHIAKI_RPCRYPT := third_party/chiaki/src/rpcrypt.c
CFLAGS := -Wall -Werror -Wno-trigraphs -g
PAIR_CFLAGS := -DCHIAKI_LIB_ENABLE_MBEDTLS -Ithird_party/chiaki/include -Ithird_party/chiaki_compat -Wno-unused-variable
MAIN_LDADD := -lkernel_sys -lSceSystemService
REMOTE_LDADD := -lkernel_sys -lSceRemoteplay -lSceSystemService -lSceRegMgr

all: $(ELF) $(ASTROREM) $(ASTROKILL) $(ASTROLOCK)

$(CHIAKI_RPCRYPT): scripts/fetch_chiaki_rpcrypt.sh
	sh scripts/fetch_chiaki_rpcrypt.sh

vendor-pairing: $(CHIAKI_RPCRYPT)

main_v12_embed.c: main_v12.c
	sed -e 's/^int main(void)$$/int astro_v12_legacy_main(void)/' -e '/add_service("websrv","WebSrv","websrv\.elf",8080);/a\  add_service("remote","Astro Remote","astrorem",45822);' $< > $@

main_v13_embed.c: main_v13.c main_v12_embed.c
	sed 's/^int main(void)$$/int astro_v13_legacy_main(void)/' main_v13.c > $@

main_v132_embed.c: main_v132.c main_v13_embed.c main_v12_embed.c
	sed 's/^int main(void)$$/int astro_v132_legacy_main(void)/' main_v132.c > $@

main_detached_build.c: main_v136.c main_v132_embed.c main_v13_embed.c main_v12_embed.c scripts/build_detached_main.py
	python3 scripts/build_detached_main.py main_v136.c $@

remote_page_split.h: remote_page_v137.h scripts/build_split_remote_page.py
	python3 scripts/build_split_remote_page.py remote_page_v137.h $@

remote_worker_safe.c: remote_worker.c remote_page_split.h scripts/build_safe_remote_worker.py
	python3 scripts/build_safe_remote_worker.py remote_worker.c $@

$(ELF): main_detached_build.c remote_service_detached.c remote_service.h remote_worker.h main_v132_embed.c main_v13_embed.c main_v12_embed.c main_v03.c astro_process_name.c
	$(CC) $(CFLAGS) -o $@ main_detached_build.c remote_service_detached.c astro_process_name.c $(MAIN_LDADD)

$(ASTROREM): $(CHIAKI_RPCRYPT) astrorem_main.c remote_worker_safe.c remote_worker.h remote_page_split.h remote_ps5_source.c remote_ps5_source.h remote_pairing.c remote_pairing.h astro_pair_client.c astro_pair_client.h astro_pair_crypto.c astro_pair_crypto.h chiaki_mbedtls_compat.c
	$(CC) $(CFLAGS) $(PAIR_CFLAGS) -o $@ astrorem_main.c remote_worker_safe.c remote_ps5_source.c remote_pairing.c astro_pair_client.c astro_pair_crypto.c chiaki_mbedtls_compat.c $(CHIAKI_RPCRYPT) $(REMOTE_LDADD)

$(ASTROKILL): astrokill.c
	$(CC) $(CFLAGS) -o $@ astrokill.c -lkernel_sys

$(ASTROLOCK): astrolock_web.c astrolock_offact.c astrolock_offact.h astrolock_process_name.c
	$(CC) $(CFLAGS) -o $@ astrolock_web.c astrolock_offact.c astrolock_process_name.c -lSceRegMgr

clean:
	rm -f $(ELF) $(ASTROREM) $(ASTROKILL) $(ASTROLOCK) main_v12_embed.c main_v13_embed.c main_v132_embed.c main_v136_embed.c main_v137_build.c main_detached_build.c remote_service_safe.c remote_worker_safe.c remote_page_split.h

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^
