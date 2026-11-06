// Tick and draw timer for the debug build. TimGetTicks is coarse (10 ms on
// most devices), so the times are summed over each log period and averaged.
// The worst tick and draw are in whole system ticks.
#include "palmbolo.h"

#ifdef DEBUG

#define kLogEvery     5    // seconds

static UInt16 gTps = 100;
static UInt32 gTickSum, gTickCount, gTickWorst;
static UInt32 gDrawSum, gDrawCount, gDrawWorst;
static UInt32 gEventSum;
static UInt32 gLastLog, gLastLogTicks;
static UInt32 gHeapMin = 0xFFFFFFFFUL;

void SEC_UI TimingInit(UInt16 tps)
{
	gTps = tps ? tps : 100;
	gLastLog = TimGetSeconds();
	gLastLogTicks = TimGetTicks();
}

// sum system ticks over count to 1/100 ms.
static UInt32 SEC_UI Avg(UInt32 sum, UInt32 count)
{
	return count ? (sum * 100000UL / gTps) / count : 0;
}

static UInt32 SEC_UI Ms100(UInt32 ticks)
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

// Logs the free dynamic heap and the free storage heap, and keeps the
// least dynamic heap seen.
void SEC_UI TimingHeap(const Char *where)
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
	return TimGetSeconds() - gLastLog >= kLogEvery;
}

void SEC_UI TimingMaybeLog(void)
{
	UInt32 now = TimGetSeconds(), ticks, a, w, d, dw, per;

	if (now - gLastLog < kLogEvery)
		return;
	ticks = TimGetTicks();
	per = ticks - gLastLogTicks;
	gLastLog = now;
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
	TimingHeap("play");
	gLastLogTicks = TimGetTicks();
	gTickSum = gTickCount = gTickWorst = 0;
	gDrawSum = gDrawCount = gDrawWorst = 0;
	gEventSum = 0;
}

#endif
