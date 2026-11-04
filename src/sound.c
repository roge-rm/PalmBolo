// Game sounds as short tones from the Sound Manager. Each frame the sounds
// the rules made are sorted near and far by distance from our tank, as
// WinBolo's sounddist.c does, and the most important one is played. Tones
// start with sndCmdFrqOn, which returns at once, and the next step of a
// sound starts from SoundFrame, so the game loop never waits on a tone.
#include "palmbolo.h"
#include "game/game.h"

#define kSoftRange  15      // map squares: further plays the far sound
#define kNoneRange  40      // map squares: this far or more plays nothing
#define kSteps      3

// What can be played: WinBolo's sound effects, plus a base capture.
enum {
	FX_NONE,
	FX_SHOOT_SELF, FX_SHOOT_NEAR, FX_SHOOT_FAR,
	FX_TREE_NEAR, FX_TREE_FAR,
	FX_BUILDING_NEAR, FX_BUILDING_FAR,
	FX_HIT_SELF, FX_HIT_NEAR, FX_HIT_FAR,
	FX_BUBBLES,
	FX_SINK_NEAR, FX_SINK_FAR,
	FX_BIG_NEAR, FX_BIG_FAR,
	FX_MINE_NEAR, FX_MINE_FAR,
	FX_LAY_MINE,
	FX_FARM_NEAR, FX_FARM_FAR,
	FX_BUILD_NEAR, FX_BUILD_FAR,
	FX_DIE_NEAR, FX_DIE_FAR,
	FX_CAPTURE,
	FX_COUNT
};

// A tone: frequency in Hz (0 is a rest), length in ms, amplitude 0 to
// sndMaxAmp (64) before the system game volume scales it.
typedef struct Tone {
	UInt16 freq;
	UInt8 ms, amp;
} Tone;

// One recipe a sound: a higher priority cuts off a lower one playing.
// Unused steps have a length of 0. Tune the sounds here.
typedef struct Recipe {
	UInt8 priority;
	Tone step[kSteps];
} Recipe;

static const Recipe kRecipe[FX_COUNT] = {
	/* FX_NONE */          { 0, { { 0, 0, 0 } } },
	/* FX_SHOOT_SELF */    { 6, { { 880, 20, 48 }, { 440, 20, 40 } } },
	/* FX_SHOOT_NEAR */    { 4, { { 660, 20, 32 }, { 330, 15, 24 } } },
	/* FX_SHOOT_FAR */     { 1, { { 440, 20, 12 } } },
	/* FX_TREE_NEAR */     { 4, { { 300, 30, 32 }, { 200, 30, 24 } } },
	/* FX_TREE_FAR */      { 1, { { 250, 30, 12 } } },
	/* FX_BUILDING_NEAR */ { 5, { { 180, 40, 40 }, { 120, 30, 32 } } },
	/* FX_BUILDING_FAR */  { 2, { { 150, 30, 14 } } },
	/* FX_HIT_SELF */      { 10, { { 120, 60, 64 }, { 90, 60, 64 } } },
	/* FX_HIT_NEAR */      { 7, { { 200, 50, 40 }, { 140, 40, 32 } } },
	/* FX_HIT_FAR */       { 3, { { 160, 40, 14 } } },
	/* FX_BUBBLES */       { 3, { { 1200, 15, 20 }, { 1500, 15, 20 }, { 1100, 15, 20 } } },
	/* FX_SINK_NEAR */     { 10, { { 400, 80, 40 }, { 300, 80, 40 }, { 200, 120, 40 } } },
	/* FX_SINK_FAR */      { 2, { { 300, 100, 14 } } },
	/* FX_BIG_NEAR */      { 9, { { 80, 80, 64 }, { 60, 120, 64 }, { 50, 150, 48 } } },
	/* FX_BIG_FAR */       { 5, { { 70, 120, 20 } } },
	/* FX_MINE_NEAR */     { 8, { { 100, 60, 56 }, { 70, 100, 48 } } },
	/* FX_MINE_FAR */      { 4, { { 90, 80, 18 } } },
	/* FX_LAY_MINE */      { 4, { { 1000, 15, 24 }, { 700, 15, 24 } } },
	/* FX_FARM_NEAR */     { 3, { { 500, 20, 24 }, { 400, 20, 24 } } },
	/* FX_FARM_FAR */      { 1, { { 450, 20, 10 } } },
	/* FX_BUILD_NEAR */    { 3, { { 600, 15, 24 }, { 0, 20, 0 }, { 600, 15, 24 } } },
	/* FX_BUILD_FAR */     { 1, { { 600, 15, 10 } } },
	/* FX_DIE_NEAR */      { 7, { { 900, 40, 32 }, { 600, 40, 32 }, { 400, 60, 32 } } },
	/* FX_DIE_FAR */       { 2, { { 600, 50, 12 } } },
	/* FX_CAPTURE */       { 7, { { 523, 60, 40 }, { 659, 60, 40 }, { 784, 100, 40 } } }
};

// The near and far sound of each kind of event (game.h SND_*). Bubbles and
// laying a mine have no far sound in WinBolo.
static const UInt8 kNear[SND_KINDS] = {
	FX_SHOOT_NEAR, FX_TREE_NEAR, FX_BUILDING_NEAR, FX_HIT_NEAR, FX_BUBBLES,
	FX_SINK_NEAR, FX_BIG_NEAR, FX_MINE_NEAR, FX_LAY_MINE, FX_FARM_NEAR,
	FX_BUILD_NEAR, FX_DIE_NEAR, FX_CAPTURE
};
static const UInt8 kFar[SND_KINDS] = {
	FX_SHOOT_FAR, FX_TREE_FAR, FX_BUILDING_FAR, FX_HIT_FAR, FX_NONE,
	FX_SINK_FAR, FX_BIG_FAR, FX_MINE_FAR, FX_NONE, FX_FARM_FAR,
	FX_BUILD_FAR, FX_DIE_FAR, FX_CAPTURE
};

static Boolean gOn;         // the app's sound option
static UInt16 gVolume;      // system game sound volume, 0 to sndMaxAmp
static UInt16 gTps;         // system ticks a second
static UInt8 gFx;           // sound playing, FX_NONE when quiet
static UInt8 gStep;         // its step playing now
static UInt32 gStepEnd;     // system tick its step ends

static void SEC_UI Quiet(void)
{
	SndCommandType c;

	MemSet(&c, sizeof(c), 0);
	c.cmd = sndCmdQuiet;
	SndDoCmd(0, &c, true);
}

// Starts step gStep of gFx, or ends the sound when there are no more.
static void SEC_UI PlayStep(UInt32 now)
{
	const Tone *t;
	SndCommandType c;
	UInt32 ticks;

	if (gStep >= kSteps || kRecipe[gFx].step[gStep].ms == 0) {
		gFx = FX_NONE;
		return;
	}
	t = &kRecipe[gFx].step[gStep];
	ticks = ((UInt32)t->ms * gTps + 999) / 1000;
	gStepEnd = now + (ticks ? ticks : 1);
	if (t->freq == 0 || t->amp == 0) {
		Quiet();
		return;
	}
	MemSet(&c, sizeof(c), 0);
	c.cmd = sndCmdFrqOn;
	c.param1 = t->freq;
	c.param2 = t->ms;
	c.param3 = (UInt16)(((UInt32)t->amp * gVolume) / sndMaxAmp);
	if (c.param3 == 0)
		return;
	SndDoCmd(0, &c, true);
}

void SEC_UI SoundOpen(UInt16 ticksPerSecond)
{
	UInt32 rom = 0;

	gTps = ticksPerSecond;
	gFx = FX_NONE;
	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
	// The volume preference and sndCmdFrqOn came with OS 3.0. Before that
	// there is only on or off, and no tone that doesn't block.
	if (rom >= sysMakeROMVersion(3, 0, 0, sysROMStageRelease, 0))
		gVolume = (UInt16)PrefGetPreference(prefGameSoundVolume);
	else
		gVolume = 0;
	if (gVolume > sndMaxAmp)
		gVolume = sndMaxAmp;
	LOG(("sound %s, game volume %d", gOn ? "on" : "off", (Int16)gVolume));
}

void SEC_UI SoundSetOn(Boolean on)
{
	gOn = on;
	if (!on && gFx != FX_NONE) {
		Quiet();
		gFx = FX_NONE;
	}
}

Boolean SEC_UI SoundIsOn(void)
{
	return gOn;
}

void SEC_UI SoundStop(void)
{
	if (gFx != FX_NONE)
		Quiet();
	gFx = FX_NONE;
}

// The sound an event makes for viewer, by its distance from the viewer's
// tank at vx, vy, or FX_NONE.
static UInt8 SEC_UI Effect(const GameSound *e, UInt8 viewer, UInt8 vx, UInt8 vy)
{
	UInt8 gx = e->x > vx ? e->x - vx : vx - e->x;
	UInt8 gy = e->y > vy ? e->y - vy : vy - e->y;
	Boolean self = e->player == viewer;

	// A hit on our tank is heard at any range, our own shot always.
	if (e->kind == SND_HIT_TANK && self)
		return FX_HIT_SELF;
	if (e->kind == SND_SHOOT && self)
		return FX_SHOOT_SELF;
	// Bubbles and sinking are only for the tank they happen to, near or far
	// by distance, as in WinBolo (tankexp.c, client_snapshot.c).
	if ((e->kind == SND_BUBBLES || e->kind == SND_TANK_SINK) && !self)
		return FX_NONE;
	if (gx >= kNoneRange || gy >= kNoneRange)
		return FX_NONE;
	if (gx <= kSoftRange && gy <= kSoftRange)
		return kNear[e->kind];
	return kFar[e->kind];
}

void SEC_UI SoundFrame(const GameState *g, UInt8 viewer)
{
	GameSound ev[GAME_MAX_SOUNDS];
	UInt8 i, n, fx, best = FX_NONE;
	UInt32 now = TimGetTicks();

	n = game_take_sounds(ev, GAME_MAX_SOUNDS);
	if (gOn && gVolume > 0 && n > 0 && viewer < g->numTanks) {
		const Tank *k = &g->tanks[viewer];
		UInt8 vx = (UInt8)(k->x >> 8), vy = (UInt8)(k->y >> 8);

		for (i = 0; i < n; i++) {
			if (ev[i].kind >= SND_KINDS)
				continue;
			fx = Effect(&ev[i], viewer, vx, vy);
			if (kRecipe[fx].priority > kRecipe[best].priority)
				best = fx;
		}
		if (best != FX_NONE &&
		    (gFx == FX_NONE || kRecipe[best].priority >= kRecipe[gFx].priority)) {
			LOG(("sound fx %d from %d events", (Int16)best, (Int16)n));
			gFx = best;
			gStep = 0;
			PlayStep(now);
			return;
		}
	}
	if (gFx != FX_NONE && (Int32)(now - gStepEnd) >= 0) {
		gStep++;
		PlayStep(now);
	}
}
