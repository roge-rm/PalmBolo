// Tick and draw timer for the debug build. TimGetTicks is coarse (10 ms on
// most devices), so the time is summed over batches and averaged.
#include "palmbolo.h"

#ifdef DEBUG

#define kTickWindow   50   // game ticks per average
#define kDrawWindow   16   // draws per average
#define kLogEvery     5    // seconds

static UInt16 gTps = 100;
static UInt32 gTickSum, gTickCount, gTickAvg;
static UInt32 gDrawSum, gDrawCount, gDrawAvg;
static UInt32 gLastLog;

void SEC_UI TimingInit(UInt16 tps)
{
	gTps = tps ? tps : 100;
	gLastLog = TimGetSeconds();
}

// Ticks in the window to 1/100 ms.
static UInt32 SEC_UI Avg(UInt32 sum, UInt32 count)
{
	return (sum * 100000UL / gTps) / count;
}

void SEC_UI TimingTicks(UInt32 start, UInt32 end, UInt16 count)
{
	gTickSum += end - start;
	gTickCount += count;
	if (gTickCount >= kTickWindow) {
		gTickAvg = Avg(gTickSum, gTickCount);
		gTickSum = gTickCount = 0;
	}
}

void SEC_UI TimingDraw(UInt32 start, UInt32 end)
{
	gDrawSum += end - start;
	if (++gDrawCount >= kDrawWindow) {
		gDrawAvg = Avg(gDrawSum, gDrawCount);
		gDrawSum = gDrawCount = 0;
	}
}

UInt32 SEC_UI TimingTickX100(void) { return gTickAvg; }
UInt32 SEC_UI TimingDrawX100(void) { return gDrawAvg; }

void SEC_UI TimingMaybeLog(void)
{
	UInt32 now = TimGetSeconds();

	if (now - gLastLog < kLogEvery)
		return;
	gLastLog = now;
	LogF("timing tick %lu.%02lu ms draw %lu.%02lu ms", gTickAvg / 100,
		gTickAvg % 100, gDrawAvg / 100, gDrawAvg % 100);
}

#endif
