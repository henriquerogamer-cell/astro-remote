# Astro Remote - PS5 payload

PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := astro_remote.elf

CFLAGS := -Wall -Wextra -O2 -g
LDADD := -lSceRegMgr -lSceUserService -lSceRemoteplay

all: $(ELF)

$(ELF): main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDADD)

clean:
	rm -f $(ELF)

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^
