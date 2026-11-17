# Infrared work

Version 0.10.0 includes two-player infrared in normal builds.

## Transport

The host advertises PalmBolo/LSAP through IAS. A joiner discovers it,
connects IrLAP, queries LSAP and opens IrLMP. Host and joiner are nodes
0 and 1. Link frames have a one-byte length and up to 64 payload bytes,
split at IrMaxTxSize. TX and RX queues hold 16 frames each. Receive
overflow ends the session. A disconnected session is not reopened, except by
a host in the lobby (below). Frames
received before a disconnect or overflow stay queued, `ir_recv` still hands
them out, and `ir_dead` reports the ended session once they are all read.
The queue is cleared when a new IrLMP connection is confirmed, and on the
host before it answers one. Connection
control has its own persistent packet and buffer; only a pending data packet
blocks the next data send. A control packet still held by IrLib is not reused
before close. OS 5 may return a copied IrPacket, so completion matches an
actually pending application buffer and its submitted length. The packet
structure address is not used to match completion.

A joiner has 10 seconds for each step (discovery, IrLAP, the LSAP query and
the IrLMP connect). When a step runs out, fails to start, or IrLMP drops
before the link was up, it disconnects IrLAP, waits up to 3 seconds for that
(asking again if it's still up) and goes back to discovery. An IrLAP confirm
that comes when no step is waiting for it is dropped the same way. Data that
arrives after the IrLMP confirm but before polling marks the link up is queued.

`NetPoll` calls `ir_sync(accepting, nodes)` before and after each lockstep
poll. `accepting` is set while the host is in the lobby, and `nodes` has a bit
for each seated node. A lobby host whose session ended (a disconnect, bad data
or overflow, where it drops IrLAP itself) waits until the old frames are read
and lockstep has freed the joiner's seat, by its LEAVE or after 3 seconds of
silence. Then it listens again and takes a connection from the same Palm or
another. An IrLMP connect that comes during the wait is answered when it ends.
Once the game has started a lost session stays dead: lockstep hands the
joiner's tank to a bot. A started host that has no session, because nobody
joined or the joiner left in the lobby, still answers one connect, and
lockstep turns that joiner away with a FULL stop.

`ir_lost` is true when the session is over and won't be replaced. A lobby
host (only when IrLib still holds the control packet) or a joiner that isn't
seated yet then stops with "Infrared connection lost." In the other cases
lockstep gives the reason.

Callbacks enter code1, recover A5 through feature Rmrf/16 and restore A5
on return. They copy data and record state changes in memory. Normal
polling handles connection, query and send calls. Ending a game retries pending
STOP or LEAVE frames for up to two seconds as queue space becomes available.
Close then drains accepted frames for up to
two seconds, then disconnects, unbinds and closes before removing the feature.

## Keeping IrLib serviced

Palm OS3.1 can strand a receive frame when RX and TX-done event bits
coalesce. Map clearing and drawing must service IR frequently enough to
keep that queue moving. The Frond diagnosis is in
`/srv/downloads/temp/debug/frond/palmbolo-ir/notes.txt`.

`ir_service` makes up to eight IrHandleEvent calls and guards against
reentry. `NetService` calls it from map storage and drawing checkpoints.
It polls lockstep only during play, outside an existing poll or game tick,
so startup cannot recursively initialise a game. It does not dispatch UI
events. Map and flag clears use 256-byte chunks while IR is active.

## Qualification

Frond 3eb74f5c passed sustained original-speed IIIx play for 180 seconds
with movement/fire and 20 agreeing checksums on each device, both actual
menu leave directions, host-only map transfer and byte-for-byte retention,
and a tick-2000 timed end. Both mixed TX/IIIx host roles passed map
retention, movement/fire, seven agreeing checksums each and identical
Time is up tables. The IIIx normal DEBUG regression passed 35 seconds
with three agreements each.

Fragmentation used the DEBUG IR_TEST_MAX_PACKET=16 clamp, including a
64-byte link frame plus its length byte. This limits the effective packet
size after IrMaxTxSize; it does not negotiate a 16-byte IrLAP MTU.
The TX status-bar menu and full dismissal also work in the focused UI check.
Game taps stay inside the form bounds, and a window-enter event restores
drawing and keys after menu dismissal. Menu-open events are recorded before
MenuHandleEvent consumes them, and repeated Menu keys do not toggle the
drawing guard. Real Palm infrared remains untested.

Reports, exact commands, hashes and screenshots:
- `/srv/downloads/temp/debug/PalmBolo/m10/qualification-20261115/B1-report.txt`
- `/srv/downloads/temp/debug/PalmBolo/m10/os5-completion-20261115/B2-report.txt`
- `/srv/downloads/temp/debug/PalmBolo/m10/final-20261115/`
- `/srv/downloads/temp/debug/PalmBolo/m10/review-fixes-20261115/`

## Repeating the check

Use Frond's current doc/using-frond.md and doc/developer-reference.md.
The runner uses palm_boot.Core and tools/link-irda.py. Reserve both heavy
emulator slots and two free IR ports in resources.md before starting.

```
heavy build make DEBUG=1
python3 tools/frond-ir.py NEW_OUTPUT --core-68k /home/dan/Projects/palm/frond/build/out-3eb74f5c86/frond-68k --iiix-session build/iiix/Palm-IIIx-3.1-en-launcher-0160.frond --seconds 60 --lobby-seconds 12
```

Default ports are 5076/5077; `--ports A B` changes them. `--core-68k`
selects a commit build; `--iiix-session` selects a launcher snapshot.
`--host tx --join iiix` selects a mixed pair. The runner records binary
and PRC hashes, commit when available, screenshots, relay summary and
app logs. It removes private firmware copies and asserts the requested
outcome in result.json. Output folders must be empty. Keep each PRC at a
distinct immutable path while both cores install it.

Use --inputs for held movement/fire and --min-sums N for checksum progress.
--expect join-leave or host-leave with --leave-ui uses the game menu.
--expect timed checks both end ticks. --map-pdb installs a second map only
on the host and compares the retained joiner database payload.
--require-fragments --max-wire 16 checks the DEBUG
IR_TEST_MAX_PACKET=16 clamp, including a 64-byte map frame and its length
byte. This tests effective packet size, not a negotiated MTU. Build that
variant in its own BUILD folder with TEST_TIME_LIMIT for timed games.

For TX use --core-arm from the same qualified build and --tx-session with
a launcher calibrated by that Frond revision. Stale touchscreen
calibration can make the fixed taps open system controls. Inspect the
stage screenshots before diagnosing a protocol failure. --arm-engine fast
is available for a normal-speed ARM run; the default remains exact.
PALMBOLO_FROND_ROOT selects a copied Frond helper tree for the documented
remote test host. Its shared speed lock still applies.

TX checks require a mixed pair and --expect timed. The runner waits for the
IIIx to report game over, then allows the peer to finish closing before
exporting the TX log. A live TX all-database export can pause IR beyond the
peer timeout. TX/TX and live-play TX checks need a non-disruptive log reader
and are rejected before starting emulators.

`EXTRA=-DIR_TRACE` enables a bounded callback recorder dumped from normal
polling. Rebuild infrared.o and the PRC when changing that flag. The normal
DEBUG build leaves it out. Earlier failed trials are retained under
`/srv/downloads/temp/debug/PalmBolo/m10/`.

`EXTRA=-DIR_TEST_CLOSE_QUEUE` fills the outgoing queue and holds transmission
for 100 ms when leaving a game. This DEBUG-only regression checks that the
pending leave controls are retried after an initial enqueue rejection.

## Before release

The 0.10.0 host and emulator release checks passed. Results are in
`/srv/downloads/temp/debug/PalmBolo/release-0.10.0-20261115/RELEASE-CHECKS.txt`.
No real Palm pair was available. Dan approved releasing 0.10.0 with physical
infrared untested.
