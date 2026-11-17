// Tick and draw timer for the debug build. TimGetTicks is coarse (10 ms on
// most devices), so the times are summed over each log period and averaged.
// The worst tick and draw are in whole system ticks.
#include "palmbolo.h"

#ifdef DEBUG

// Every 5 s by the system ticks, which stop with the CPU under a
// debugger, where the clock in seconds runs on.
#define kLogEvery     5    // seconds

static UInt16 gTps = 100;
static UInt32 gTickSum, gTickCount, gTickWorst;
static UInt32 gDrawSum, gDrawCount, gDrawWorst;
static UInt32 gEventSum;
static UInt32 gLastLogTicks;
static UInt32 gHeapMin = 0xFFFFFFFFUL;
// Gaps between frames in 10 ms bins (the last is 150 ms and more), game
// ticks run before each frame (the last is 8 and more), the longest gap.
#define kGapBins      16
#define kRunBins      9
static UInt16 gGaps[kGapBins], gRuns[kRunBins];
static UInt32 gLastFrame, gGapWorst;

#ifdef LOOP_PARTS
// Debug: wall time split by what the event loop was doing. Each mark
// charges the time since the last mark to a part, so the parts add up to
// the whole period. Before OS 5 the DragonBall's timer 1, which makes the
// system tick, gives the time within a tick: counter at 0xFFFFF608,
// compare (counts a tick) at 0xFFFFF604, the same on the 328, EZ and VZ.
// Elsewhere marks fall back on whole system ticks.
#define kTmrCompare   ((volatile UInt16 *)0xFFFFF604UL)
#define kTmrCounter   ((volatile UInt16 *)0xFFFFF608UL)
static UInt16 gFineDiv;                 // timer counts a system tick, 0 if none
static UInt32 gPartT[LP_COUNT], gMarks, gPartStart;
static Int32 gPartC[LP_COUNT];
static UInt32 gWaits, gWaitAsked, gNils, gEvents;

// Time in 1/gFineDiv system ticks, wrapping; differences are exact.
static UInt32 SEC_UI Fine(void)
{
	UInt32 t;
	UInt16 c;

	if (!gFineDiv)
		return TimGetTicks();
	do {
		t = TimGetTicks();
		c = *kTmrCounter;
	} while (TimGetTicks() != t);
	return t * gFineDiv + c;
}

// Fine units to microseconds.
static UInt32 SEC_UI Us(UInt32 f)
{
	UInt32 div = gFineDiv ? gFineDiv : 1, perTick = 1000000UL / gTps;

	return f / div * perTick + f % div * perTick / div;
}

// One system tick call and no multiply (a slow library call on the 68000),
// as marks come often: the counter read either side of TimGetTicks shows if
// it wrapped meanwhile. Whole ticks and counts are summed apart.
static UInt32 gMarkT;
static UInt16 gMarkC;

void SEC_UI TimingMark(UInt8 part)
{
	UInt32 t;
	UInt16 c1, c2;

	if (gFineDiv) {
		do {
			c1 = *kTmrCounter;
			t = TimGetTicks();
			c2 = *kTmrCounter;
		} while (c2 < c1);
	} else {
		t = TimGetTicks();
		c2 = 0;
	}
	gPartT[part] += t - gMarkT;
	gPartC[part] += (Int32)c2 - gMarkC;
	gMarkT = t;
	gMarkC = c2;
	gMarks++;
}

// Fine time in microseconds, for short debug timings.
UInt32 SEC_UI TimingNowUs(void)
{
	return Us(Fine());
}

void SEC_UI TimingWait(Int32 wait, Boolean nil)
{
	if (wait > 0) {
		gWaits++;
		gWaitAsked += wait;
	}
	if (nil)
		gNils++;
	else
		gEvents++;
}

static void SEC_COLD FineInit(void)
{
	UInt32 rom = 0, t0;
	UInt16 i;

	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
	if (rom < 0x05000000UL && *kTmrCompare > 100)
		gFineDiv = *kTmrCompare;
	t0 = Fine();
	for (i = 0; i < 100; i++)
		TimingMark(LP_OTHER);
	LogF("loop parts: timer %u a sys tick, 100 marks %lu us", gFineDiv, Us(Fine() - t0));
	t0 = Fine();
	for (i = 0; i < 100; i++)
		KeyCurrentState();
	LogF("100 KeyCurrentState %lu us", Us(Fine() - t0));
	TimingMark(LP_OTHER);
	for (i = 0; i < LP_COUNT; i++)
		gPartT[i] = gPartC[i] = 0;
	gMarks = 0;
	gPartStart = Fine();
}

// Logs the parts in thousandths of the period.
static void SEC_COLD PartsLog(void)
{
	const Char *names = "sleep\0poll\0sys\0handle\0input\0tick\0ticklog\0"
		"sound\0view\0panel\0debug\0log\0other\0";
	Char buf[150];
	const Char *n = names;
	UInt32 now = Fine(), all = Us(now - gPartStart), sum = 0, v;
	UInt16 i;

	gPartStart = now;
	if (!all)
		all = 1;
	buf[0] = 0;
	for (i = 0; i < LP_COUNT; i++) {
		v = Us(gPartT[i] * (gFineDiv ? gFineDiv : 1) + gPartC[i]);
		sum += v;
		StrCat(buf, " ");
		StrCat(buf, n);
		StrCat(buf, " ");
		StrIToA(buf + StrLen(buf), (Int32)(v * 1000 / all));
		n += StrLen(n) + 1;
		gPartT[i] = gPartC[i] = 0;
	}
	LogF("loop parts per mille:%s", buf);
	LogF("loop parts: %lu ms in all, %lu ms marked, %lu marks, %lu waits asking %lu sys ticks,"
		" %lu nil and %lu other events", all / 1000, sum / 1000, gMarks, gWaits, gWaitAsked,
		gNils, gEvents);
	gMarks = gWaits = gWaitAsked = gNils = gEvents = 0;
}
#endif

#ifdef SEARCH_PARTS
// Debug: times the bots' path search with the DragonBall's timer 1 (OS 3
// and 4), which counts up to its compare value once a system tick. bot.c
// calls search_mark(0) before a step and search_mark(1) after it, and
// counts the cells in gSearchCells. Sums are in timer counts.
#define kSrCompare    ((volatile UInt16 *)0xFFFFF604UL)
#define kSrCounter    ((volatile UInt16 *)0xFFFFF608UL)
UInt32 gSearchCells, gSearchExpand, gSearchTake, gSearchSlow, gSearchSlowN, gSearchLaunch, gSearchLaunchN;
UInt16 gSearchDiv;      // timer counts a system tick
static UInt16 gSearchC;
static UInt32 gSearchT, gSearchSum, gSearchSteps, gSearchWorst;

void SEC_UI search_mark(UInt8 end)
{
	UInt32 t, d;
	UInt16 c;

	if (!gSearchDiv)
		return;
	do {
		t = TimGetTicks();
		c = *kSrCounter;
	} while (TimGetTicks() != t);
	if (end) {
		d = (t - gSearchT) * gSearchDiv + c - gSearchC;
		gSearchSum += d;
		gSearchSteps++;
		if (d > gSearchWorst)
			gSearchWorst = d;
	}
	gSearchT = t;
	gSearchC = c;
}

// Timer counts to microseconds.
static UInt32 SEC_COLD SearchUs(UInt32 f)
{
	UInt32 perTick = 1000000UL / gTps;

	return f / gSearchDiv * perTick + f % gSearchDiv * perTick / gSearchDiv;
}

static void SEC_COLD SearchLog(void)
{
	if (gSearchDiv)
		LogF("search %lu us in %lu steps, %lu cells, worst step %lu us, expand %lu us, take %lu us",
			SearchUs(gSearchSum), gSearchSteps, gSearchCells, SearchUs(gSearchWorst),
			SearchUs(gSearchExpand), SearchUs(gSearchTake));
	if (gSearchDiv)
		LogF("search parts: C expand %lu us in %lu, launch %lu us in %lu",
			SearchUs(gSearchSlow), gSearchSlowN, SearchUs(gSearchLaunch), gSearchLaunchN);
	gSearchSlow = gSearchSlowN = gSearchLaunch = gSearchLaunchN = 0;
	gSearchSum = gSearchSteps = gSearchCells = gSearchWorst = gSearchExpand = gSearchTake = 0;
}
#endif

#ifdef EXPAND_CHECK
#include "game/game.h"

// Debug: room for two copies of the search state, for the check in bot.c of
// expand68k.S against the C, and its count in the log.
extern unsigned long gExpandChecked, gExpandDiffer;
extern UInt16 gExpandFirst;
static BotSearch *gCheckCopy[2];

BotSearch *SEC_UI search_check_copy(BotSearch *q, UInt8 which, UInt8 back)
{
	if (!gCheckCopy[which])
		gCheckCopy[which] = MemPtrNew(sizeof(BotSearch));
	if (!gCheckCopy[which])
		return NULL;
	if (back)
		MemMove(q, gCheckCopy[which], sizeof(BotSearch));
	else
		MemMove(gCheckCopy[which], q, sizeof(BotSearch));
	return gCheckCopy[which];
}

static void SEC_COLD ExpandCheckLog(void)
{
	LogF("expand check %lu cells, %lu differ, first at cell %u%s", gExpandChecked, gExpandDiffer,
		gExpandFirst, gCheckCopy[1] ? "" : ", no memory for the copies");
}
#endif

void SEC_COLD TimingInit(UInt16 tps)
{
	gTps = tps ? tps : 100;
	gLastLogTicks = TimGetTicks();
#ifdef SEARCH_PARTS
	{
		UInt32 rom = 0;

		FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
		if (rom < 0x05000000UL && *kSrCompare > 100)
			gSearchDiv = *kSrCompare;
	}
#endif
#ifdef LOOP_PARTS
	FineInit();
#endif
}

// sum system ticks over count to 1/100 ms.
static UInt32 SEC_COLD Avg(UInt32 sum, UInt32 count)
{
	return count ? (sum * 100000UL / gTps) / count : 0;
}

static UInt32 SEC_COLD Ms100(UInt32 ticks)
{
	return ticks * 100000UL / gTps;
}

// One game tick took ticks system ticks.
void SEC_UI TimingTick(UInt32 ticks)
{
	gTickSum += ticks;
	gTickCount++;
	if (ticks > gTickWorst)
		gTickWorst = ticks;
}

void SEC_UI TimingEvent(UInt32 ticks)
{
	gEventSum += ticks;
}

void SEC_UI TimingDraw(UInt32 start, UInt32 end)
{
	gDrawSum += end - start;
	gDrawCount++;
	if (end - start > gDrawWorst)
		gDrawWorst = end - start;
}

void SEC_UI TimingFrame(UInt16 ticks)
{
	UInt32 now = TimGetTicks(), gap = now - gLastFrame, bin;

	if (gLastFrame) {
		bin = gap * 100 / gTps;
		gGaps[bin < kGapBins ? bin : kGapBins - 1]++;
		if (gap > gGapWorst)
			gGapWorst = gap;
	}
	gRuns[ticks < kRunBins ? ticks : kRunBins - 1]++;
	gLastFrame = now;
}

// Appends the counts in bins to buf as " a b c", each at most 9999.
static void SEC_COLD Bins(Char *buf, UInt16 *bins, UInt16 n)
{
	UInt16 i;

	for (i = 0; i < n; i++) {
		buf += StrLen(buf);
		*buf++ = ' ';
		StrIToA(buf, bins[i] < 9999 ? bins[i] : 9999);
		bins[i] = 0;
	}
}

// Logs the free dynamic heap and the free storage heap, and keeps the
// least dynamic heap seen.
void SEC_COLD TimingHeap(const Char *where)
{
	UInt32 dynFree = 0, dynMax = 0, stFree = 0, stMax = 0;

	MemHeapFreeBytes(MemHeapID(0, 0), &dynFree, &dynMax);
	MemHeapFreeBytes(MemHeapID(0, 1), &stFree, &stMax);
	if (dynFree < gHeapMin)
		gHeapMin = dynFree;
	LogF("heap %s: dynamic free %lu (biggest %lu, least seen %lu) storage free %lu",
		where, dynFree, dynMax, gHeapMin, stFree);
}

Boolean SEC_UI TimingDue(void)
{
	return TimGetTicks() - gLastLogTicks >= (UInt32)kLogEvery * gTps;
}

void SEC_COLD TimingMaybeLog(void)
{
	UInt32 ticks, a, w, d, dw, per;

	if (!TimingDue())
		return;
	ticks = TimGetTicks();
	per = ticks - gLastLogTicks;
	gLastLogTicks = ticks;
	if (!gTickCount && !gDrawCount)
		return;
	if (!per)
		per = 1;
	a = Avg(gTickSum, gTickCount);
	w = Ms100(gTickWorst);
	d = Avg(gDrawSum, gDrawCount);
	dw = Ms100(gDrawWorst);
	// Frames and game ticks a second, in hundredths.
	LogF("timing %lu ticks avg %lu.%02lu ms worst %lu.%02lu, %lu draws avg %lu.%02lu ms worst %lu.%02lu,"
		" events %lu sys ticks, in %lu sys ticks: %lu.%02lu fps %lu.%02lu tps",
		gTickCount, a / 100, a % 100, w / 100, w % 100,
		gDrawCount, d / 100, d % 100, dw / 100, dw % 100, gEventSum, per,
		gDrawCount * gTps / per, gDrawCount * gTps * 100 / per % 100,
		gTickCount * gTps / per, gTickCount * gTps * 100 / per % 100);
	{
		// 25 bins of up to 4 digits and a space, " /" and the end.
		Char buf[kGapBins * 5 + kRunBins * 5 + 3];

		buf[0] = 0;
		Bins(buf, gGaps, kGapBins);
		StrCat(buf, " /");
		Bins(buf, gRuns, kRunBins);
		LogF("frames worst %lu ms gaps%s",
			Ms100(gGapWorst) / 100, buf);
		gGapWorst = 0;
		// The gap over the logging itself isn't counted.
		gLastFrame = 0;
	}
	TimingHeap("play");
#ifdef LOOP_PARTS
	PartsLog();
#endif
#ifdef SEARCH_PARTS
	SearchLog();
#endif
#ifdef EXPAND_CHECK
	ExpandCheckLog();
#endif
	gLastLogTicks = TimGetTicks();
	gTickSum = gTickCount = gTickWorst = 0;
	gDrawSum = gDrawCount = gDrawWorst = 0;
	gEventSum = 0;
}

#endif
