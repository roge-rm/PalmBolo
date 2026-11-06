// Keyboard play: built-in keyboards (Clie TG50, NX, UX, Treo, Centro) and
// attached ones (Palm Universal and Wireless, Stowaway, GoType) all give
// keyDownEvent characters. Letters, arrows and the rest are turned into
// the same held keys the buttons give, or into one-off actions.
//
// keyDownEvent has no release on most keyboards, so a held key lasts a
// hold window from each press or repeat. While a game is up the system
// repeat is made quick (200 ms delay, 50 ms period at most) so the window
// can be short and a released key stops soon. Keyboards that do say when
// a key goes up are held until then: Sony sends vchrSonyKeyDown and
// vchrSonyKeyUp around each key, palmOne marks a key that will send a
// keyUpEvent with willSendUpKeyMask.
#include "palmbolo.h"
#include "game/game.h"

#define vchrSonyJogUp       0x1700
#define vchrSonyJogDown     0x1701
#define vchrSonyJogPush     0x1705
#define vchrSonyJogRelease  0x1706
#define vchrSonyKeyDown     0x1720  // keyCode is the character
#define vchrSonyKeyUp       0x1721
#define kWillSendUp         0x0800  // willSendUpKeyMask, palmOne
#define kKeyUpEvent         0x4000  // keyUpEvent, OS 5 with palmOne keys

#define kNavLeft    0x01000000UL    // KeyCurrentState bits, as in main.c
#define kNavRight   0x02000000UL
#define kTapTicks   2               // game ticks a first press of fire or mine holds
#define kHeld       6               // IN_LEFT to IN_MINE
#define kUntilUp    0xFFFFFFFFUL

static UInt32 gUntil[kHeld];        // sys tick each held key runs to, 0 not held
static UInt32 gStart[kHeld];        // tick of a first press waiting for its first repeat, 0 none
static UInt16 gFirst = 50;          // longest first repeat delay seen, ticks
static Boolean gNavState;           // KeyCurrentState gives the 5-way's left and right
static UInt16 gInit = 50, gPeriod = 10, gTps = 100;
static UInt16 gOldInit, gOldPeriod, gOldTap;
static Boolean gOldAhead, gRatesSet;

// What a character does: an IN_* held key (low byte) or 0x100 plus a KA_*
// action. 0 for nothing.
static UInt16 SEC_KEYS Lookup(WChar c)
{
	if (c >= 'A' && c <= 'Z')
		c = (WChar)(c + 'a' - 'A');
	switch (c) {
	case chrLeftArrow: case 'o': case vchrRockerLeft:
		return IN_LEFT;
	case chrRightArrow: case 'p': case vchrRockerRight:
		return IN_RIGHT;
	case chrUpArrow: case 'q':
		return IN_FASTER;
	case chrDownArrow: case 'a':
		return IN_SLOWER;
	case chrSpace: case chrLineFeed: case chrCarriageReturn: case vchrSonyJogPush:
		return IN_FIRE;
	case chrHorizontalTabulation: case 'z':
		return IN_MINE;
	case 'k': case ',': case vchrSonyJogDown:
		return 0x100 | KA_NEAR;
	case 'l': case '.': case vchrSonyJogUp:
		return 0x100 | KA_FAR;
	case 't': case '1':
		return 0x100 | KA_TREE;
	case 'r': case '2':
		return 0x100 | KA_ROAD;
	case 'w': case '3':
		return 0x100 | KA_WALL;
	case 'g': case '4':
		return 0x100 | KA_PILL;
	case 'm': case '5':
		return 0x100 | KA_MINE;
	case 'v':
		return 0x100 | KA_OVERVIEW;
	case chrBackspace: case chrEscape:
		return 0x100 | KA_MENU;
	}
	return 0;
}

static UInt8 SEC_KEYS Index(UInt16 bit)
{
	UInt8 i = 0;

	while (i < kHeld - 1 && !(bit & (1 << i)))
		i++;
	return i;
}

void SEC_KEYS KeysClear(void)
{
	UInt8 i;

	for (i = 0; i < kHeld; i++)
		gUntil[i] = gStart[i] = 0;
}

void SEC_KEYS KeysStart(UInt16 ticksPerSecond)
{
	UInt16 init, period, tap;
	Boolean ahead;

	gTps = ticksPerSecond ? ticksPerSecond : 100;
	gFirst = gInit;
	KeysClear();
	if (gRatesSet || KeyRates(false, &gOldInit, &gOldPeriod, &gOldTap, &gOldAhead) != errNone)
		return;
	init = gOldInit;
	period = gOldPeriod;
	if (init > gTps / 5)
		init = gTps / 5;
	if (period > gTps / 20)
		period = gTps / 20;
	if (period < 1)
		period = 1;
	tap = gOldTap;
	ahead = gOldAhead;
	if (KeyRates(true, &init, &period, &tap, &ahead) == errNone) {
		gRatesSet = true;
		gInit = init;
		gPeriod = period;
	} else {
		gInit = gOldInit;
		gPeriod = gOldPeriod;
	}
	gFirst = gInit;
	LOG(("key repeat %u then %u ticks (was %u, %u)", gInit, gPeriod, gOldInit, gOldPeriod));
}

void SEC_KEYS KeysStop(void)
{
	KeysClear();
	if (gRatesSet) {
		KeyRates(true, &gOldInit, &gOldPeriod, &gOldTap, &gOldAhead);
		gRatesSet = false;
	}
}

// A press or repeat of a held key: held through the gap to the next repeat
// (the first one comes after gInit, the rest every gPeriod), or until it
// goes up when the keyboard says it will.
static void SEC_KEYS Press(UInt16 bit, Boolean repeat, Boolean untilUp)
{
	UInt8 i = Index(bit);
	UInt32 now = TimGetTicks(), until;

	if (untilUp) {
		gUntil[i] = kUntilUp;
		return;
	}
	if (gUntil[i] == kUntilUp)
		return;
	if (repeat) {
		// Some keyboards ignore KeyRates; the first repeat's delay sets
		// the window for later first presses.
		if (gStart[i]) {
			UInt32 d = now - gStart[i];

			if (d > gFirst && d <= gTps)
				gFirst = (UInt16)d;
			gStart[i] = 0;
		}
		until = now + 2 * gPeriod + gTps / 50 + 1;
	} else {
		gStart[i] = now | 1;
		// A tap of fire or mine is held for a couple of game ticks only,
		// so it acts once; repeats keep it going.
		if (bit & (IN_FIRE | IN_MINE))
			until = now + kTapTicks * (gTps / 50 + 1);
		else
			until = now + gFirst + gPeriod + gTps / 50 + 1;
	}
	if (gUntil[i] < until)
		gUntil[i] = until;
}

UInt8 SEC_KEYS KeysEvent(const EventType *e)
{
	WChar c;
	UInt16 mods, what;

	if (e->eType == kKeyUpEvent) {
		LOG(("key up chr %x code %x mods %x", e->data.keyDown.chr, e->data.keyDown.keyCode,
			e->data.keyDown.modifiers));
		what = Lookup(e->data.keyDown.chr);
		if (!what)
			what = Lookup(e->data.keyDown.keyCode);
		if (what && what < 0x100)
			gUntil[Index(what)] = 0;
		return what ? KA_EAT : KA_NONE;
	}
	if (e->eType != keyDownEvent)
		return KA_NONE;
	c = e->data.keyDown.chr;
	mods = e->data.keyDown.modifiers;
	// Sony's key down and up come before and after the character. They
	// stay with the system, which makes the character from them.
	if (c == vchrSonyKeyDown || c == vchrSonyKeyUp) {
		what = Lookup(e->data.keyDown.keyCode);
		if (what && what < 0x100) {
			if (c == vchrSonyKeyDown)
				Press(what, false, true);
			else
				gUntil[Index(what)] = 0;
		}
		return KA_NONE;
	}
	if (c == vchrSonyJogRelease) {
		gUntil[Index(IN_FIRE)] = 0;
		return KA_EAT;
	}
	if (c == vchrLateWakeup || c == vchrAutoOff) {
		KeysClear();
		return KA_NONE;
	}
	if (c == vchrRockerLeft || c == vchrRockerRight) {
		// Where KeyCurrentState has the 5-way it is read there.
		if (KeyCurrentState() & (kNavLeft | kNavRight))
			gNavState = true;
		if (gNavState)
			return KA_EAT;
	}
	what = Lookup(c);
	if (!what) {
		// Menus, commands, Find and the launcher stay with the system;
		// other characters don't reach the form.
		return (mods & commandKeyMask) ? KA_NONE : KA_EAT;
	}
	if (what >= 0x100)
		return (UInt8)(KA_EAT | (what & 0xFF));
	Press(what, (mods & (autoRepeatKeyMask | doubleTapKeyMask)) != 0,
		(mods & kWillSendUp) != 0 || c == vchrSonyJogPush);
	return KA_EAT;
}

#ifdef DEBUG
static UInt8 gLogged;
#endif

UInt8 SEC_KEYS KeysHeld(void)
{
	UInt32 now = TimGetTicks();
	UInt8 i, keys = 0;

	if (KeyCurrentState() & (kNavLeft | kNavRight))
		gNavState = true;

	for (i = 0; i < kHeld; i++)
		if (gUntil[i]) {
			if (gUntil[i] != kUntilUp && (Int32)(now - gUntil[i]) >= 0)
				gUntil[i] = 0;
			else
				keys = (UInt8)(keys | (1 << i));
		}
#ifdef DEBUG
	if (keys != gLogged) {
		LOG(("keyboard keys %x", (UInt16)keys));
		gLogged = keys;
	}
#endif
	return keys;
}

// The map square under the viewer's gunsight, where keyboard builder
// orders go.
void SEC_KEYS KeysTarget(const struct GameState *g, UInt8 *mx, UInt8 *my)
{
	uint16_t wx, wy;

	game_gunsight(g, game_viewer_tank(g), &wx, &wy);
	*mx = (UInt8)((wx + 128) >> 8);
	*my = (UInt8)((wy + 128) >> 8);	LOG(("keyboard build at %u,%u", (UInt16)*mx, (UInt16)*my));
}
