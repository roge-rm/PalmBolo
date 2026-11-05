# Builds build/PalmBolo.prc with prc-tools-remix and PilRC.
#   make             release PRC
#   make DEBUG=1     adds the debug log, timers and self-test (build-debug/)
#   make host-test   builds and runs the game rules test on the host
#   make build/maprender   host tool: maprender FILE.map OUT.png [8|16 [X Y W H]]
#   make art-preview       PNGs of the art sheets at every depth in build/art/preview
# EXTRA="-DAUTOQUIT_SECONDS=9" makes a debug build quit by itself, and
# EXTRA="-DSELFTEST_AT_START" runs the self-test at launch (it is on the menu),
# EXTRA="-DDEMO_START" puts the tank west of the first base for replays,
# EXTRA="-DKEEP_INPUT_AREA" leaves the input area open on a TX, and
# EXTRA="-DDEMO_BUILDER" by a forest on Everard Island with no trees.
# EXTRA="-DDEMO_SHOWCASE" puts a parachute, carried pills and a wreck on
# screen in a new game.
# EXTRA="-DTEST_TIME_LIMIT=1000" ends a debug game after that many ticks.
# EXTRA="-DTEST_LEAVE_AT=500" leaves a debug game for the title at that tick.
# EXTRA="-DBOT_COUNT=3 -DBOT_LEVEL=2" sets the bots in a new game (level
# 1 easy, 2 normal, 3 hard; 3 normal bots by default).

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
ART      = build/art
CFLAGS   = -O2 -Wall -Wno-unknown-pragmas $(DEFS) $(INCLUDES) -I$(ART) -Isrc
LDFLAGS  = -L$(SDK)/lib/m68k-palmos-coff

SRCS     = main screen detect dlog timing mapload mapstore_palm tileview tilepick panel sound front
GAMESRCS = game tank shells pills lgm treegrow util map bot
# The scripted games are for the debug self-test only.
ifdef DEBUG
GAMESRCS += script
endif
OBJS     = $(addprefix $(BUILD)/,$(addsuffix .o,$(SRCS) $(GAMESRCS)))

all: build/$(PRC)

$(BUILD):
	mkdir -p $(BUILD)

# The art: tools/art2palm.py turns res/art into bitmaps, the pilrc bitmap
# families (art.rcp) and the cell numbers (art.h).
ARTSRC   = tools/art2palm.py res/art/palette.txt res/art/layout.txt $(wildcard res/art/*.png)

# One run writes all three, so deleting any of them makes it again.
$(ART)/stamp $(ART)/art.h $(ART)/art.rcp &: $(ARTSRC)
	python3 tools/art2palm.py $(ART)
	touch $(ART)/stamp

art-preview: $(ARTSRC)
	mkdir -p $(ART)/preview
	python3 tools/art2palm.py $(ART) --preview $(ART)/preview

$(BUILD)/%.o: src/%.c src/palmbolo.h src/resource.h src/game/*.h src/tilepick.h $(ART)/art.h | $(BUILD)
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

$(BUILD)/palmbolo.ro: src/palmbolo.rcp src/resource.h res/everard.map $(ART)/art.rcp | $(BUILD)
	pilrc -q -ro $(if $(DEBUG),-D DEBUG) -I src -I $(ART) src/palmbolo.rcp $@

# The PRC is always written to build/PalmBolo.prc, so build one flavour at a time.
build/$(PRC): src/palmbolo.def $(BUILD)/palmbolo $(BUILD)/palmbolo.ro
	build-prc -o $@ src/palmbolo.def $(BUILD)/palmbolo $(BUILD)/palmbolo.ro

host-test: build/hosttest
	build/hosttest

HOSTSRC  = src/game/game.c src/game/bot.c src/game/tank.c src/game/shells.c src/game/pills.c src/game/lgm.c src/game/treegrow.c src/game/script.c src/game/util.c src/game/map.c host/mapstore_host.c

build/hosttest: host/hosttest.c $(HOSTSRC) src/game/*.h | $(BUILD)
	gcc -Wall -Wextra -O2 -fsanitize=address,undefined host/hosttest.c $(HOSTSRC) -lm -o $@

# maprender reads the colour sheets from build/art at run time.
build/maprender: host/maprender.c src/tilepick.c src/tilepick.h $(HOSTSRC) src/game/*.h $(ART)/stamp | $(BUILD)
	gcc -Wall -Wextra -O2 -Isrc -I$(ART) -DART_DIR='"$(ART)"' host/maprender.c src/tilepick.c $(HOSTSRC) -o $@

clean:
	rm -rf build

.PHONY: all clean host-test art-preview
