| expand for the Palm: one cell of the path search of the bots, as expand in
| bot.c does it for a cell well inside the map. A cell near the edge goes
| to the C (expand_c). The results must be the same as
| the C to the bit, as a lockstep game can mix devices; a DEBUG build with
| -DEXPAND_CHECK runs both and counts any difference.
|
| uint8_t expand_asm(GameState *s, uint16_t idx), prc-tools calling: args
| on the stack, 8(a6) s and 12(a6) idx after the link, result in d0. d0-d2
| and a0-a1 are free, d3-d7 and a2-a4 are saved, a5 holds the globals.
| The C helpers it calls (bot.c) keep d3-d7 and a2-a4 too.
|
| Frame: -8(a6) to -1(a6) c[0..7], the step cost each way; -10(a6) x and
| -12(a6) y, as words; -13(a6) escape, 2 until asked; -14(a6) zone;
| -15(a6) mode: 0 no boat, 1 afloat, 2 a boat that has landed.
|
| Registers all through: a4 q (s->search), d7 idx until the costs, then
| ok. a2 rows, &mapstore_rows[y]. d6 x, except in the neighbour checks.
| Neighbour checks: a3 dn (&q->distance[idx]), d3 least, d4 need, d5 shut,
| d2 zone, a0 q->cell, a1 q->danger. Costs: d5 want, a3 kLandCost.
| Arrivals: a3 dn again, d4 need, d5 zone.

#include "expand68k.h"

	.section bots,"x"
	.even

| Directions d 0 to 7 are N NE E SE S SW W NW, bit d in each mask, as in
| bot.c. Window cells a step moves: -32 -31 1 33 32 31 -1 -33.

| LOOK bit, step: as LOOK in bot.c. d0 o, d1 n, d6 scratch.
	.macro LOOK bit, step
	btst #\bit,%d5
	bne.s 9f
	move.w \step*2(%a3),%d0
	cmp.w %d3,%d0
	bls.s 9f
	move.w %d7,%d1
	add.w #\step,%d1
	cmp.w #-1,%d0
	bne.s 2f
	| Not reached yet: solid if its nibble is 15.
	move.w %d1,%d6
	lsr.w #1,%d6
	move.b (%a0,%d6.w),%d6
	btst #0,%d1
	beq.s 1f
	lsr.b #4,%d6
1:	not.b %d6
	and.b #15,%d6
	bne.s 2f
	bset #\bit,%d5
	bra.s 9f
	| In a pill zone only if dearer than least and ZONE_WAIT.
2:	move.w %d1,%d6
	lsr.w #3,%d6
	btst %d1,(%a1,%d6.w)
	bne.s 3f
	bset #\bit,%d4
	bra.s 9f
3:	move.w %d3,%d6
	addq.w #8,%d6
	cmp.w %d6,%d0
	bls.s 9f
	bset #\bit,%d4
	bset #\bit,%d2
9:
	.endm

| COST bit, dx, dy, sides: the step cost of a wanted direction into c[],
| a diagonal only with both sides ok.
	.macro COST bit, dx, dy, sides
	btst #\bit,%d5
	beq.s 9f
	.if \sides
	move.b %d7,%d0
	and.b #\sides,%d0
	cmp.b #\sides,%d0
	bne.s 9f
	.endif
	lea \dy*4(%a2),%a0
	move.w %d6,%d0
	.if \dx
	add.w #\dx,%d0
	.endif
	bsr cost_one
	tst.b %d0
	beq.s 9f
	move.b %d0,-8+\bit(%a6)
	bset #\bit,%d7
9:
	.endm

| RELAX bit, step: a cheaper arrival takes the new distance, its parent
| direction and a heap entry.
	.macro RELAX bit, step
	btst #\bit,%d4
	beq.s 9f
	moveq #0,%d0
	move.b -8+\bit(%a6),%d0
	add.w %d3,%d0
	subq.w #1,%d0
	btst #\bit,%d5
	beq.s 1f
	addq.w #8,%d0
1:	cmp.w \step*2(%a3),%d0
	bcc.s 9f
	move.w %d0,\step*2(%a3)
	move.w #\step,%d0
	moveq #\bit+1,%d1
	bsr nib_push
9:
	.endm

| launch_candidate(s, idx, 2, x, y), with y then x pushed already.
	.macro LAUNCH2
	move.b #2,-(%sp)
	move.w %d7,-(%sp)
	move.l 8(%a6),-(%sp)
	bsr.w expand_launch
	lea 12(%sp),%sp
	.endm

	.globl expand_asm
expand_asm:
	link.w %a6,#-16
	movem.l %d3-%d7/%a2-%a4,-(%sp)
	move.l 8(%a6),%a4
	lea EX_SEARCH(%a4),%a4
	move.w 12(%a6),%d7
	moveq #31,%d0
	and.w %d7,%d0                   | d0 lx
	move.w %d7,%d1
	lsr.w #5,%d1                    | d1 ly
	| d2 h, the larger of the two distances, 32 more off the edge facing
	| a goal out of the window.
	lea gRawX@END.w(%a5),%a0
	move.b (%a0,%d0.w),%d2
	lea gRawY@END.w(%a5),%a0
	move.b (%a0,%d1.w),%d3
	cmp.b %d3,%d2
	bcc.s 1f
	move.b %d3,%d2
1:	tst.b gFar@END.w(%a5)
	beq.s 2f
	cmp.b gEdgeX@END.w(%a5),%d0
	beq.s 2f
	cmp.b gEdgeY@END.w(%a5),%d1
	beq.s 2f
	cmp.b #223,%d2
	bls.s 3f
	st %d2
	bra.s 2f
3:	add.b #32,%d2
2:	cmp.b EX_NEAR(%a4),%d2
	bhi.s 4f
	| Near enough: the goal is reached.
	move.w %d7,EX_BEST(%a4)
	move.b %d2,EX_BESTH(%a4)
	moveq #1,%d0
	bra .Lret

	| d6 x, d5 y. A cell near the map edge is left to the C.
4:	moveq #0,%d6
	move.b EX_OX(%a4),%d6
	add.b %d0,%d6
	moveq #0,%d5
	move.b EX_OY(%a4),%d5
	add.b %d1,%d5
	move.b %d6,%d3
	sub.b #23,%d3
	cmp.b #210,%d3
	bhi .LtoC
	move.b %d5,%d3
	sub.b #23,%d3
	cmp.b #210,%d3
	bhi .LtoC

	move.w %d6,-10(%a6)
	move.w %d5,-12(%a6)
	lea mapstore_rows@END.w(%a5),%a2
	move.w %d5,%d3
	lsl.w #2,%d3
	add.w %d3,%a2
	move.l (%a2),%a0
	move.b (%a0,%d6.w),%d4          | d4 here
	moveq #0,%d3
	tst.b EX_BOAT(%a4)
	beq.s 3f
	| A boat is afloat on water (river, deep sea, a boat), else landed.
	moveq #1,%d3
	cmp.b #1,%d4
	beq.s 2f
	cmp.b #-1,%d4
	beq.s 2f
	cmp.b #9,%d4
	beq.s 2f
	moveq #2,%d3
2:	move.b %d3,-15(%a6)
	bra.s 5f
3:	move.b %d3,-15(%a6)
	cmp.b #9,%d4
	bne.s 5f
	| A boat to board is a launch, and goes no further.
	move.b %d5,-(%sp)
	move.b %d6,-(%sp)
	move.b #1,-(%sp)
	move.w %d7,-(%sp)
	move.l 8(%a6),-(%sp)
	bsr.w expand_launch
	lea 12(%sp),%sp
	bra .Lret0

	| The nearest cell so far: less h, then less of both distances.
5:	cmp.b EX_BESTH(%a4),%d2
	bcs.s 6f
	bne.s 7f
	lea gRawX@END.w(%a5),%a0
	lea gRawY@END.w(%a5),%a1
	moveq #0,%d3
	move.b (%a0,%d0.w),%d3
	moveq #0,%d0
	move.b (%a1,%d1.w),%d0
	add.w %d0,%d3
	move.w EX_BEST(%a4),%d1
	move.w %d1,%d0
	and.w #31,%d0
	lsr.w #5,%d1
	move.b (%a0,%d0.w),%d0
	move.b (%a1,%d1.w),%d1
	add.w %d1,%d0
	cmp.w %d0,%d3
	bcc.s 7f
6:	move.b %d2,EX_BESTH(%a4)
	move.w %d7,EX_BEST(%a4)

	| From land, river beside it N E S W is somewhere to launch a boat.
7:	tst.b -15(%a6)
	bne.w 8f
	cmp.b #1,%d4
	beq.w 8f
	cmp.b #-1,%d4
	beq.w 8f
	move.l -4(%a2),%a0
	cmp.b #1,(%a0,%d6.w)
	bne.s 1f
	move.w %d5,%d0
	subq.b #1,%d0
	move.b %d0,-(%sp)
	move.b %d6,-(%sp)
	LAUNCH2
1:	move.l (%a2),%a0
	cmp.b #1,1(%a0,%d6.w)
	bne.s 1f
	move.b %d5,-(%sp)
	move.w %d6,%d0
	addq.b #1,%d0
	move.b %d0,-(%sp)
	LAUNCH2
1:	move.l 4(%a2),%a0
	cmp.b #1,(%a0,%d6.w)
	bne.s 1f
	move.w %d5,%d0
	addq.b #1,%d0
	move.b %d0,-(%sp)
	move.b %d6,-(%sp)
	LAUNCH2
1:	move.l (%a2),%a0
	cmp.b #1,-1(%a0,%d6.w)
	bne.s 8f
	move.b %d5,-(%sp)
	move.w %d6,%d0
	subq.b #1,%d0
	move.b %d0,-(%sp)
	LAUNCH2

	| d5 shut: neighbours out of the window.
8:	moveq #0,%d5
	moveq #31,%d0
	and.w %d7,%d0
	move.w %d7,%d1
	lsr.w #5,%d1
	tst.w %d1
	bne.s 1f
	or.b #0x83,%d5
1:	cmp.w #31,%d0
	bne.s 1f
	or.b #0x0E,%d5
1:	cmp.w #31,%d1
	bne.s 1f
	or.b #0x38,%d5
1:	tst.w %d0
	bne.s 1f
	or.b #0xE0,%d5
1:	lea EX_DISTANCE(%a4),%a3
	add.w %d7,%a3
	add.w %d7,%a3
	move.w (%a3),%d3
	addq.w #1,%d3                   | d3 least
	moveq #0,%d4
	moveq #0,%d2
	lea EX_CELL(%a4),%a0
	lea EX_DANGER(%a4),%a1
	LOOK 0, -32
	LOOK 1, -31
	LOOK 2, 1
	LOOK 3, 33
	LOOK 4, 32
	LOOK 5, 31
	LOOK 6, -1
	LOOK 7, -33
	tst.b %d4
	beq .Lret0
	move.b %d2,-14(%a6)

	| d5 want: the needed directions and the sides of needed diagonals.
	move.b %d4,%d0
	and.b #0xAA,%d0
	move.b %d4,%d1
	move.b %d0,%d2
	add.b %d2,%d2
	or.b %d2,%d1
	move.b %d0,%d2
	lsr.b #1,%d2
	or.b %d2,%d1
	move.b %d0,%d2
	lsr.b #7,%d2
	or.b %d2,%d1
	not.b %d5
	and.b %d1,%d5
	move.w -10(%a6),%d6
	moveq #0,%d7
	move.b #2,-13(%a6)
	lea kLandCost@END.w(%a5),%a3
	| Straight first, as a diagonal needs both sides open.
	COST 0, 0, -1, 0
	COST 2, 1, 0, 0
	COST 4, 0, 1, 0
	COST 6, -1, 0, 0
	COST 1, 1, -1, 0x05
	COST 3, 1, 1, 0x14
	COST 5, -1, 1, 0x50
	COST 7, -1, -1, 0x41

	and.b %d7,%d4
	beq .Lret0
	move.b -14(%a6),%d5
	lea EX_DISTANCE(%a4),%a3
	move.w 12(%a6),%d0
	add.w %d0,%a3
	add.w %d0,%a3
	RELAX 0, -32
	RELAX 1, -31
	RELAX 2, 1
	RELAX 3, 33
	RELAX 4, 32
	RELAX 5, 31
	RELAX 6, -1
	RELAX 7, -33
.Lret0:
	moveq #0,%d0
.Lret:
	movem.l (%sp)+,%d3-%d7/%a2-%a4
	unlk %a6
	rts
.LtoC:
	movem.l (%sp)+,%d3-%d7/%a2-%a4
	unlk %a6
	bra.w expand_c

| d0 = the step cost into nx d0.w on row a0 (a pointer into mapstore_rows),
| as the C for a tank with no boat: a boat 1, else cost_land, then the
| shoreline escape for river. a3 kLandCost. Uses d0-d2, a0-a1.
cost_one:
	move.l (%a0),%a1
	moveq #0,%d1
	move.b (%a1,%d0.w),%d1          | d1 nt
	tst.b -15(%a6)
	bne.w .Lboat
	cmp.b #9,%d1
	beq.s 7f
.Lland:
	cmp.b #10,%d1
	bcs.s 1f
	cmp.b #16,%d1
	bcs.w 5f
	cmp.b #-1,%d1
	beq.s 6f
	moveq #1,%d2
	bra.s 2f
	| kLandCost is 0 for walls.
1:	move.b (%a3,%d1.w),%d2
	beq.s 6f
	| Deep sea on or beside it is out.
2:	cmp.b #-1,-1(%a1,%d0.w)
	beq.s 3f
	cmp.b #-1,1(%a1,%d0.w)
	beq.s 3f
	move.l -4(%a0),%a1
	cmp.b #-1,(%a1,%d0.w)
	beq.s 3f
	move.l 4(%a0),%a1
	cmp.b #-1,(%a1,%d0.w)
	beq.s 3f
	move.b %d2,%d0
	rts
3:	cmp.b #1,%d1
	beq.s 4f
6:	moveq #0,%d0
	rts
7:	moveq #1,%d0
	rts
	| River by deep sea: step_cost with the escape, if this cell allows it.
	| d1 ny.
4:	move.l %a0,%d1
	lea mapstore_rows@END.w(%a5),%a1
	sub.l %a1,%d1
	lsr.l #2,%d1
	move.b -13(%a6),%d2
	cmp.b #2,%d2
	bne.s 1f
	movem.l %d0-%d1,-(%sp)
	move.b -11(%a6),-(%sp)
	move.b %d6,-(%sp)
	move.l 8(%a6),-(%sp)
	bsr.w expand_open
	addq.l #8,%sp
	tst.b %d0
	seq %d2
	and.b #1,%d2
	move.b %d2,-13(%a6)
	movem.l (%sp)+,%d0-%d1
1:	tst.b %d2
	beq.s 6b
	move.b %d1,-(%sp)
	move.b %d0,-(%sp)
	move.l 8(%a6),-(%sp)
	bsr.w expand_escape
	addq.l #8,%sp
	rts
	| On a boat: water costs 1 afloat and is out once landed. Land from
	| afloat is a landing, step_cost in the C. Land once landed costs as
	| it does with no boat, and is not river, so has no escape.
.Lboat:
	cmp.b #1,%d1
	beq.s 1f
	cmp.b #9,%d1
	beq.s 1f
	cmp.b #-1,%d1
	beq.s 1f
	cmp.b #2,-15(%a6)
	beq.w .Lland
	move.l %a0,%d2
	lea mapstore_rows@END.w(%a5),%a1
	sub.l %a1,%d2
	lsr.l #2,%d2
	move.b %d2,-(%sp)
	move.b %d0,-(%sp)
	move.l 8(%a6),-(%sp)
	bsr.w expand_landing
	addq.l #8,%sp
	rts
1:	moveq #0,%d0
	cmp.b #1,-15(%a6)
	bne.s 2f
	moveq #1,%d0
2:	rts
	| A mine: the C, as it may need the known mines.
5:	move.b %d1,-(%sp)
	move.l %a0,%d1
	lea mapstore_rows@END.w(%a5),%a1
	sub.l %a1,%d1
	lsr.l #2,%d1
	move.b %d1,-(%sp)
	move.b %d0,-(%sp)
	move.l 8(%a6),-(%sp)
	bsr.w expand_mine
	lea 10(%sp),%sp
	rts

| Sets the nibble of cell idx + d0.w to d1 and pushes the cell on the heap,
| as set_nib and heap_push. In the arrivals, where d6, d7 and a2 are free.
| d2 n, d6 a, d7 the axes sum then a parent key.
nib_push:
	move.w 12(%a6),%d2
	add.w %d0,%d2
	move.w %d2,%d0
	lsr.w #1,%d0
	lea EX_CELL(%a4),%a0
	add.w %d0,%a0
	btst #0,%d2
	beq.s 1f
	lsl.b #4,%d1
	and.b #0x0F,(%a0)
	or.b %d1,(%a0)
	bra.s 2f
1:	and.b #0xF0,(%a0)
	or.b %d1,(%a0)
2:	cmp.w #1024,EX_COUNT(%a4)
	bcc.w 8f
	moveq #31,%d0
	and.w %d2,%d0                   | d0 lx
	move.w %d2,%d1
	lsr.w #5,%d1                    | d1 ly
	lea gHX@END.w(%a5),%a0
	moveq #0,%d6
	move.b (%a0,%d0.w),%d6
	lea gHY@END.w(%a5),%a0
	moveq #0,%d7
	move.b (%a0,%d1.w),%d7
	cmp.b %d6,%d7
	bls.s 3f
	move.b %d7,%d6
3:	lea gTX@END.w(%a5),%a0
	moveq #0,%d7
	move.b (%a0,%d0.w),%d7
	lea gTY@END.w(%a5),%a0
	move.b (%a0,%d1.w),%d0
	add.w %d0,%d7
	| f, less fbase. One too big for the key goes to the C, which
	| moves fbase.
	lea EX_DISTANCE(%a4),%a0
	move.w %d2,%d0
	add.w %d0,%d0
	move.w (%a0,%d0.w),%d0
	add.w %d6,%d0
	sub.w EX_FBASE(%a4),%d0
	cmp.w #2047,%d0
	bhi.s 8f
	lsl.w #5,%d0
	or.w %d6,%d0
	swap %d0
	moveq #10,%d1
	lsl.w %d1,%d7
	or.w %d2,%d7
	move.w %d7,%d0                  | d0 key
	| Up from the end while the parent is no less.
	lea EX_HEAP(%a4),%a0
	move.w EX_COUNT(%a4),%d1
	addq.w #1,EX_COUNT(%a4)
	lsl.w #2,%d1                    | d1 at, a byte offset
	beq.s 5f
4:	move.w %d1,%d6
	subq.w #4,%d6
	asr.w #1,%d6
	and.w #-4,%d6                   | d6 parent
	move.l (%a0,%d6.w),%d7
	cmp.l %d0,%d7
	bcs.s 5f
	move.l %d7,(%a0,%d1.w)
	move.w %d6,%d1
	bne.s 4b
5:	move.l %d0,(%a0,%d1.w)
	rts
8:	move.w %d2,-(%sp)
	move.l %a4,-(%sp)
	bsr.w expand_push
	addq.l #6,%sp
	rts
