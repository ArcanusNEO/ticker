MAKEFLAGS += -r
.PHONY: all clean extern
.SUFFIXES: .c .o .d
SRC := $(shell find . -path ./src -prune -o -type f -name "*.c" -print)
OBJ := $(patsubst %.c, %.o, $(SRC))
DEP := $(patsubst %.c, %.d, $(SRC))

EXT := cmacs.h include/liburing.h lib/liburing.a
WGET := wget -qc --show-progress -t 3 --waitretry=3

CFLAGS ?= -O3 -fno-plt -pipe -flto=auto
CFLAGS += -D_GNU_SOURCE=1 -fPIC -fwrapv -fms-extensions -Wall -Wvla -Wno-microsoft -I$(CURDIR) -I$(CURDIR)/extern -I$(CURDIR)/include
LDFLAGS ?= -Wl,-O1
LDFLAGS += -L$(CURDIR)/lib
LDLIBS += -l:liburing.a -lm

all: main

main: $(OBJ) $(EXT)
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@

%.o: %.c
	$(COMPILE.c) $< -o $@

clean:
	$(RM) -- $(OBJ) $(DEP) main

-include $(DEP)
%.d: %.c $(EXT)
	$(COMPILE.c) -MM $< -o $@

extern: $(EXT)
	install -d $@

cmacs.h:
	@$(WGET) -O $@ https://raw.github.com/ArcanusNEO/cmacs/master/cmacs.h || ($(RM) $@ && false)

include/liburing.h lib/liburing.a&:
	@$(WGET) -O liburing.tar.gz https://api.github.com/repos/axboe/liburing/tarball/liburing-2.15 && \
	echo '32ad6e4468788d9eb7eb9b65caa22b224e63c7b242080fee055d5ed2215d7a1d400376b54cca21a4845e818496d84513471b275554f4acf829c9ca887637a7a7  liburing.tar.gz' | b2sum -c || ($(RM) liburing.tar.gz && false)
	install -d src/liburing
	@tar -xf liburing.tar.gz -C src/liburing --strip-components=1
	@$(RM) liburing.tar.gz
	cd src/liburing && \
	./configure --prefix=$(CURDIR) && \
	$(MAKE) -C src ENABLE_SHARED=0 && \
	$(MAKE) -C src install ENABLE_SHARED=0

.SECONDARY: $(OBJ)
