# Infrared work

Infrared is experimental and only appears in DEBUG builds. Release builds
keep the 0.9.0 links. M10 still needs its release checks.

## Transport

The host advertises PalmBolo/LSAP through IAS. A joiner discovers it,
connects IrLAP, queries LSAP and opens IrLMP. Host and joiner are nodes
0 and 1. Link frames have a one-byte length and up to 64 payload bytes,
split at IrMaxTxSize. TX and RX queues hold 16 frames each. Receive
overflow stops delivery. A disconnected session is not reopened.

Callbacks enter code1, recover A5 through feature Rmrf/16 and restore A5
on return. They copy data and record state changes in memory. Normal
polling handles connection, query and send calls. Close drains for up to
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

Two original-speed IIIx devices on Frond3eb74f5c stayed connected for a
60-second observation with six agreeing checksums on both devices. No
player dropped or became a bot. Release/DEBUG compile and segcheck pass;
fresh review found no new blocker or major. Evidence and passing DEBUG PRC:
`/srv/downloads/temp/debug/PalmBolo/m10/ir-service-20261114/`.

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
app logs. It removes private firmware copies. It does not assert a pass;
check both logs for advancing checksum agreements and no bot takeover.

`EXTRA=-DIR_TRACE` enables a bounded callback recorder dumped from normal
polling. Rebuild infrared.o and the PRC when changing that flag. The normal
DEBUG build leaves it out. Earlier failed trials are retained under
`/srv/downloads/temp/debug/PalmBolo/m10/`.

## Still open

Lockstep ignores rejected transmit enqueues and can consume a reliable
link's only checksum copy while the IR queue is full. Fix that recovery
case before release. Map transfer, graceful leave, timed completion,
small-packet fragmentation and slow/fast pairs remain release checks.
