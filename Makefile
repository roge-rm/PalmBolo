# Builds build/PalmBolo.prc with prc-tools-remix and PilRC.
#   make             release PRC
#   make DEBUG=1     adds the debug log, timers and self-test (build-debug/)
#   make host-test   builds and runs the game rules test on the host
#   make build/maprender   host tool: maprender FILE.map OUT.png [TILE_PX [X Y W H]]
# EXTRA="-DAUTOQUIT_SECONDS=9" makes a debug build quit by itself, and
# EXTRA="-DSELFTEST_AT_START" runs the self-test at launch (it is on the menu),
# EXTRA="-DDEMO_START" puts the tank west of the first base for replays, and
# EXTRA="-DDEMO_BUILDER" by a forest on Everard Island with no trees.

SDK      = ../palm-os-sdk/sdk-5r3
PRC      = PalmBolo.prc

ifdef DEBUG
BUILD    = build/debug
DEFS     = -DDEBUG $(EXTRA)
else
BUILD    = build/release
DEFS     = $(EXTRA)
endif

CC       = m68k-palmos-gcc
INCLUDES = $(addprefix -I,$(shell find $(SDK)/include -type d))
CFLAGS   = -O2 -Wall -Wno-unknown-pragmas $(DEFS) $(INCLUDES)
LDFLAGS  = -L$(SDK)/lib/m68k-palmos-coff

SRCS     = main screen detect dlog timing mapload mapstore_palm tileview
GAMESRCS = game tank shells pills lgm treegrow util map tiles
# The scripted games are for the debug self-test only.
ifdef DEBUG
GAMESRCS += script
endif
OBJS     = $(addprefix $(BUILD)/,$(addsuffix .o,$(SRCS) $(GAMESRCS)))

all: build/$(PRC)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c src/palmbolo.h src/resource.h src/game/*.h | $(BUILD)
	@echo CC $<; $(CC) $(CFLAGS) -c $< -o $@

$(addprefix $(BUILD)/,$(addsuffix .o,$(GAMESRCS))): $(BUILD)/%.o: src/game/%.c src/game/*.h | $(BUILD)
	@echo CC $<; $(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/palmbolo-sections.s $(BUILD)/palmbolo-sections.ld: src/palmbolo.def | $(BUILD)
	m68k-palmos-multigen -b $(BUILD)/palmbolo-sections src/palmbolo.def

$(BUILD)/palmbolo-sections.o: $(BUILD)/palmbolo-sections.s
	$(CC) -c $< -o $@

# segcheck fails the build on references that link but go wrong on the
# device (extern const tables, calls across code segments).
$(BUILD)/palmbolo: $(OBJS) $(BUILD)/palmbolo-sections.o $(BUILD)/palmbolo-sections.ld
	python3 tools/segcheck.py $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(BUILD)/palmbolo-sections.o $(BUILD)/palmbolo-sections.ld -o $@

$(BUILD)/palmbolo.ro: src/palmbolo.rcp src/resource.h res/icon.bmp res/smallicon.bmp res/everard.map | $(BUILD)
	pilrc -q -ro $(if $(DEBUG),-D DEBUG) -I src src/palmbolo.rcp $@

# The PRC is always written to build/PalmBolo.prc, so build one flavour at a time.
build/$(PRC): src/palmbolo.def $(BUILD)/palmbolo $(BUILD)/palmbolo.ro
	build-prc -o $@ src/palmbolo.def $(BUILD)/palmbolo $(BUILD)/palmbolo.ro

host-test: build/hosttest
	build/hosttest

HOSTSRC  = src/game/game.c src/game/tank.c src/game/shells.c src/game/pills.c src/game/lgm.c src/game/treegrow.c src/game/script.c src/game/util.c src/game/map.c src/game/tiles.c host/mapstore_host.c

build/hosttest: host/hosttest.c $(HOSTSRC) src/game/*.h | $(BUILD)
	gcc -Wall -Wextra -O2 -fsanitize=address,undefined host/hosttest.c $(HOSTSRC) -lm -o $@

build/maprender: host/maprender.c $(HOSTSRC) src/game/*.h | $(BUILD)
	gcc -Wall -Wextra -O2 host/maprender.c $(HOSTSRC) -o $@

clean:
	rm -rf build

.PHONY: all clean host-test
