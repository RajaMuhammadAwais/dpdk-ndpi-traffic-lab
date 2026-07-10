PKGCONF = pkg-config

CFLAGS += -O3 $(shell $(PKGCONF) --cflags libdpdk libndpi)
LDFLAGS += $(shell $(PKGCONF) --libs libdpdk libndpi) -lrte_hash

main: main.c
	gcc $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f main
