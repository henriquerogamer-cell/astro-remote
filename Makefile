# Astro - PS5 administration payload

PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := astro_remote.elf
SRC := astro_admin.c
DEPS := astro_part1.inc astro_part2.inc astro_part3.inc astro_part4.inc astro_part5.inc

CFLAGS := -Wall -Wextra -O2 -g
LDADD :=

all: $(ELF)

$(ELF): $(SRC) $(DEPS)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDADD)

clean:
	rm -f $(ELF)

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^
