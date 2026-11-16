# Lockstep protocol

How Palms play one game together. The code is `src/net/lockstep.c` and the link interface `src/net/link.h`.

## Idea

Every Palm runs the whole game. Only the players' inputs travel. A turn is 4 ticks (80 ms). The host gathers every player's input for a turn, puts them together and sends the turn to everyone, and each Palm steps turn T only once it has turn T. So every Palm steps the same game with the same inputs.

The host is slot 0 and player n drives tank n. Bots come after the players.

## Links

A link carries frames of up to 64 bytes between nodes. The host is node 0; each joiner gets a node of its own. Joiners only talk to the host.

- IrLAP and L2CAP links are reliable: frames arrive in order and none are lost. Each turn and each input is sent once, and sent again from what the other side has only when nothing new has been acknowledged for twice the round trip plus 40 ms (200 ms before a round trip is measured).
- A UDP link can lose, repeat and reorder frames. Then every frame carries all the turns or inputs the other side hasn't acknowledged, and one goes out when there's something new or 60 ms after the last.

Send positions, checksum copies and retry timers advance when the link accepts a frame. A full send queue leaves them pending. Roster and START batches resume at the first name still waiting, and each STOP round waits for every peer to accept its frame.

Turn numbers in frames tell the receiver where things go, so the same messages work on both kinds of link. The loopback link (`LINK_LOOP`) can act as either.

## Turns and delay

- The input a player makes during turn T is for turn T + delay. Turns before the delay have no inputs, so the game starts at once.
- The delay is in turns, 1 to 7, agreed at start. `ls_delay_for(rtt)` suggests one from the measured round trip: enough turns of 80 ms to cover the round trip plus 40 ms, plus one, at least 2.
- Round trips come from PING and PONG once a second, each way, smoothed (7/8 old, 1/8 new). `ls_rtt` gives the longest.
- Each player's input for a turn is the keys of each of its 4 ticks, the options (auto slowdown, auto gunsight) and one builder order. The order is given on the turn's first tick; the last one tapped in a turn wins.
- Anything that changes the game is in the input or agreed at start: keys after button mapping, gunsight steps, options, builder orders, names, teams. What only the screen sees (the viewer, seen mines, sound) stays on each Palm.

## Waiting and dropped players

- When the next turn isn't complete the game waits. After 300 ms `ls_waiting` names the slots waited for: the host knows; joiners are told by the host, or wait for the host itself.
- The host hands a player's tank to a bot when the player sends LEAVE, or when their input for the turn being put together is missing and nothing has come from them for the drop time (3 s). The handover is part of the turn, so every Palm makes the tank a bot as that turn starts. The bot plays at the game's bot level (normal if there are no bots).
- A joiner who hears nothing from the host for the drop time stops with "host lost". The host sends a player handed to a bot no more turns or PINGs, and answers anything they send with STOP 7, so they stop either way, also when they come back after a menu was open or the beam was blocked. There's no joining a game already running.

## The end of a timed game

The game is over on the same tick everywhere. Each Palm then calls `ls_finish` and keeps polling until `ls_finished`, at most 5 s (`LS_END_MS`), before it leaves and shows the results:
- The host waits until every player has acknowledged the turns up to the end, then sends STOP 8 (the end) in place of STOP 2.
- A joiner waits for STOP 8 and leaves without a LEAVE, so no bot takes over at the end. A joiner still playing when STOP 8 comes runs on to the end with the turns it has.

## Checksums

- Every 64 turns (5.12 s), as a turn ends, each Palm takes `ls_checksum`: the CRC of the game state (`game_checksum`) xor a CRC of 16 rows of terrain and their known-mine bits, rows 16 × ((tick / 256) mod 16). The whole map is covered every 82 s. A whole-map CRC takes too long on a 16 MHz Palm.
- Joiners send theirs to the host and the host sends its own to everyone, riding on INPUT and TURNS frames (once on a reliable link, in the next 8 frames on a lossy one). A checksum that arrives before the Palm's own for that tick waits for it.
- A mismatch stops the game with the tick and both checksums, and STOP goes to everyone. The loopback debug build saves both states to the database `PalmBoloDesync` (record 0: tick, our checksum, theirs, slot as four 32-bit words; records 1 and 2: the two `GameState`s). The host test only reports.

## Starting

1. Joiner sends HELLO every 500 ms until START comes.
2. The host seats each new node in the next slot. Once a second, and whenever the seats change, it sends each joiner SEATS (their slot, who is seated, the bots, teams and the map's name) and the other players' NAMEs, so joiners see the lobby.
3. A joiner who sends LEAVE, or is silent for 3 s, loses their seat and those after move up a slot, so players stay in slots 0 up. A joiner who hears nothing from the host for 3 s once seated stops with "host lost".
4. When the host starts the game it sends START and every player's NAME to each joiner, again every 200 ms (or the resend time) until that joiner's READY or MAPGET.
5. A joiner with START and all names looks for the map. If it lacks it, it gets it (below). Then it sets up its game from START (`ls_setup`, `game_init` with the seed) and sends READY.
6. The host plays once every joiner is READY. While the game starts either side allows 10 s of silence (`LS_START_MS`), as loading a map takes a while on a slow Palm. A joiner silent that long is handed to a bot.

START is all a Palm needs to set up the same game: the seed, the map's checksum and length, the game type, hidden mines, time limit, bots and their level, teams, delay, the checksum interval and the drop time. The map's checksum is the CRC-32 of the `.map` file's bytes, and its length is at most 65535.

## Sending the map

A joiner that has no map with START's checksum and length asks for it (`ls_join_fetch`). Built-in Everard Island counts as a map everyone has.

- The map goes in MAPDATA frames of 61 bytes at offsets that are multiples of 61. The joiner sends MAPGET with the bytes it has in order, and a mask of chunks after them that came early; it keeps up to 8 chunks ahead and drops the rest. It sends MAPGET when it has 4 more chunks, 150 ms after the last, or 40 ms after a chunk comes early.
- The host sends up to 8 chunks past what the joiner has. When a MAPGET's mask shows chunks came after a missing one, it sends the missing ones again at once, at most once a round trip. When what the joiner has doesn't move for the resend time plus 100 ms (2 s over a reliable link) it starts again from there.
- The joiner checks the CRC of the whole map against START; a mismatch, or no room to keep it, stops it with reason 6, and the host hands its tank to a bot. With the map in, MAPGET says so and the host sends START again until READY.
- On the Palm the bytes go into the database `PalmBoloMapIn` as they come, and the whole map becomes a `Map ` database, as a HotSync'd map, named after the map, with " 2", " 3" and so on after it if that name is taken. Maps are found by checksum, so two maps with the same name don't get mixed up.

Over the loopback with 5 to 40 ms of delay a 20 KB map takes about 2.5 s; with 15% of frames lost and reordering about 10 s. Infrared throughput depends on the negotiated baud rate and packet size.

## Messages

Byte 0 is the type. Numbers are big-endian. Turns are sent as their low 16 bits and read as the turn nearest the receiver's own, at most 16 away.

| Type | Name | From | Bytes after the type |
|---|---|---|---|
| 1 | HELLO | joiner | version (1), name (up to 15 bytes, no 0) |
| 2 | START | host | see below |
| 3 | NAME | host | slot (plus 0x80 for a START name), name (up to 15 bytes) |
| 4 | READY | joiner | slot |
| 5 | INPUT | joiner | see below |
| 6 | TURNS | host | see below |
| 7 | PING | both | time (4), the sender's ms |
| 8 | PONG | both | the PING's time sent back |
| 9 | STOP | both | reason, slot, tick (4), checksum (4), other checksum (4) |
| 10 | LEAVE | joiner | slot |
| 16 | MAPGET | joiner | bytes it has (2), mask of early chunks (by chunk number mod 8) |
| 17 | MAPDATA | host | offset (2), up to 61 bytes of the map |
| 18 | SEATS | host | your slot, seated slots, bots, teams, the map's name (up to 31 bytes) |

START, 30 bytes and the map's name: type, version, your slot, players, seed (4), map checksum (4), map length (2), game type, hidden mines, time limit (minutes), bots, bot level, delay, checksum interval (turns), drop time (seconds), 8 bytes of allies (bit n of byte t: tank t's player is allied to player n), then the map's name (up to 31 bytes, no 0).

INPUT: type, slot, next turn wanted (2), first turn (2), count byte, inputs, then a checksum if bit 7 of the count byte is set. The next turn wanted is the first turn the joiner doesn't have; it acknowledges every turn before it.

TURNS: type, next input wanted (2), first turn (2), count byte, a wait byte if bit 6 of the count byte is set (slots the host is waiting for), the turns, then a checksum if bit 7 is set. Each turn is a byte of slots whose inputs follow, a byte of slots handed to bots as the turn starts, then the inputs of those slots in slot order. The count is in bits 0-5.

An input is 2 to 7 bytes: a head byte (options in bits 0-1, builder order 0-5 in bits 2-4, bit 5 set when all 4 ticks have the same keys), then one key byte or four, then the order's square x and y if there's an order.

A checksum is the tick (4) then the checksum (4).

STOP reasons: 1 checksums differ, 2 the host left, 3 host lost, 4 another protocol version, 5 no seat left or the game has started, 6 the map didn't arrive, 7 a bot has your tank, 8 the game is over. A STOP from a joiner other than a desync is taken as LEAVE.

NAMEs in the lobby are sent with the slot alone, those with START with 0x80 added. A joiner takes lobby NAMEs only before START and START NAMEs only after it, so one that comes late can't change a name. A START that comes again is ignored.

## Sizes

A turn of 8 players with keys changing every tick and builder orders is 58 bytes, so a TURNS frame always fits one turn. Usually an input is 2 bytes, so a 2-player turn is 6 bytes and an INPUT frame 9. At 12.5 turns a second that is about 230 bytes a second each way, with pings, for 2 players.

## Testing

- `make host-test` runs each lockstep game once for 3000 ticks; `build/hosttest --lockstep SEEDS TICKS` runs them for more seeds and longer. The games: IR-like and UDP-like links (delay, loss, repeats, reordering), a slow device, a player who goes silent, one who leaves, and a desync made on a joiner and on the host. Every Palm still playing must end with the same checksum and the same state byte for byte, and the desyncs must be caught at the first checksum after them. Every joiner must see the lobby as the host has it before the start.
- The map games (map-ir, map-udp with 15% loss and reordering, lobby-drop) start with joiners that lack the map: a 20000 byte map (Everard Island with bytes after it) must arrive byte for byte and the games end the same. map-udp has a player leave the lobby, lobby-drop one go silent in it, and in map-ir and lobby-drop someone tries to join after the start and must be turned away.
- pause and pause-udp stop polling a joiner for 4 or 5 s, as with a menu open, and then go on: it must stop with reason 7 and its tank be a bot's. timed and timed-ir play a one minute game: every Palm must end on the same tick with the same state, no bots for players, the host with reason 8 and joiners quietly, each within 2.5 s of its end.
- `LOCK_ONLY=backpressure build/hosttest --lockstep 1 1024` checks rejected send state and runs a reliable game with one queued frame per sender. The lobby, START names and 20000 byte map must arrive, and both peers must finish with matching states and checksum comparisons. `LOCK_ONLY=name` selects any other scenario.
- On the Palm, a debug build with `EXTRA=-DLOOPBACK_TEST` adds a "Loopback" link to Host and Join game, with a second copy of the game in the app at the other end on a map store of its own. When you host it joins and gets the map (it has only Everard Island); when you join it hosts on the map last picked, starts 5 s after you join, and you get the map from it. Add `-DLOOPBACK_DROP_AT=N` (the second copy goes silent after N seconds) or `-DLOOPBACK_DESYNC_AT=N` (its state changes at tick N). `tools/m9-net.replay` hosts and then joins a game on a TX in Frond.

## UDP link

The Network link uses NetLib datagram sockets, with one socket on each Palm.
The host binds the selected port (5046 by default); joiners use ephemeral local
ports and send to a numeric IPv4 address. NetLib supplies the configured Palm
connection. There is no discovery, encryption, authentication or router setup.

The link wraps each existing lockstep frame in a 16-byte header:

| Bytes | Contents |
| --- | --- |
| 0–3 | `PBUD` |
| 4 | Wrapper version, 1 |
| 5 | Payload size, 1–64 |
| 6–7 | Reserved, zero |
| 8–11 | Host session, big-endian; zero in the initial HELLO |
| 12–15 | Joiner session, big-endian |

Session values distinguish app/link openings; they are not authentication.
A joiner accepts replies only from its configured IP/port with its session
value, and remembers the host's session from the first reply. The host maps
an IP/port plus joiner session to a link node. Only a correctly framed HELLO
can reserve a node. An unknown joiner receives the existing STOP reason for a
full/started game or incompatible lockstep version without taking a node.

After each poll, unseated lobby nodes are freed; lockstep's existing silence
expiry releases abandoned lobby seats. Nodes are never reassigned during a
match. The lockstep slot-to-node table remains authoritative when lobby seats
move after a departure. Stale sessions, wrong sources and oversized or
malformed datagrams are discarded. Receive work is bounded to 32 datagrams per
poll; UDP loss and rejected sends use the existing lockstep retry paths.

The adapter uses zero-timeout, nonblocking socket operations. NetLib owns its
connection dialog and cancellation. Open counts determine which reference the
app owns, including partial opens; closing the game releases that reference
without forcing other applications off the network. A new join times out after
10 seconds without being seated. Socket errors stop the local game with a
reason; lockstep handles peers who leave or go silent as on other links.

`make udp-test` runs framing checks, production adapter calls against a NetLib
fault-injection stub, and eight lockstep peers over real host loopback UDP
sockets. The latter includes map transfer, loss and send backpressure, full and
late join rejection, lobby seat reuse, player leave and host leave, with state
and terrain checksums at 1024 ticks. This does not establish Palm NetLib or
physical network interoperability; those checks remain in the release list.


## Bluetooth link

Bluetooth uses L2CAP with a 64-byte local MTU and minimum remote MTU, one
lockstep frame per packet. The host creates a discoverable master piconet and
advertises a listener with service UUID `d71c8923-a64b-4e80-b010-526d72660001`.
Joiners use Palm's device picker, connect the ACL link, query that service by
SDP and connect to the returned PSM. The host accepts three joiners; excess,
duplicate and late connections are rejected. Discovery has the native Cancel
button; pending connection stages time out after 30 seconds in total.

Socket callbacks copy data into bounded per-peer queues. An overflowing or
invalid reliable receive drops that peer instead of silently losing a frame.
Each peer has one send buffer, unavailable until SendComplete or a synchronous
successful return. Busy sends return backpressure to lockstep. Completion
checks the buffer and length; callbacks from an earlier link opening are
ignored. The first-segment callbacks restore the application's A5 before
calling the Bluetooth segment.

The foreground poll closes failed peer sockets and releases departed lobby
ACL links owned by the host piconet. Node identities cannot be reused during a
match. Losing a joiner leaves the others running until lockstep hands its tank
to a bot; losing the host ends a joiner's game. Radio errors stop the local game.
Normal event dispatch services Bluetooth; long operations and bounded close
waits peek at the system key queue and consume only library hooks. Ordinary
keys and application events stay queued in their original order.

Closing releases the app's sockets, SDP record, callbacks and library reference.
A joiner's pre-existing ACL is borrowed and is not explicitly disconnected.
Hosting refuses an already-open library so it does not take over another app's
piconet; only a successfully created piconet is destroyed on exit.

`make bt-test` builds five isolated instances of the production adapter against
a controlled asynchronous BtLib. Four run the actual lockstep and map-transfer
code; the fifth exercises full/late rejection. The test checks map bytes,
1024 matching game ticks and terrain checksums, lobby reuse, dropped-player
bot takeover, host leave and busy sends. Fault cases cover missing hardware,
open/registration/SDP/socket failures, cancelled discovery, borrowed ACL cleanup,
pending/synchronous sends, receive overflow, old callback epochs and timeouts.
This is not a radio interoperability test. Multi-Palm Frond checks where supported
and physical-device checks are recorded separately in the release checklist.

## Offline continuation (M14)

`ls_offline` finishes only an already-started buffered turn, at most three
simulation ticks, with that turn's received inputs. It never starts a future
turn. There is no rollback snapshot or guarantee that every peer acknowledged
this boundary. A known desync, setup failure or announced game end cannot
become offline play. A time limit reached while finishing the partial turn
uses normal game completion.

After the boundary, other human tanks become bots at the match's bot level.
Existing bots keep their state. The local tank stays in its original slot,
including a dropped player's tank reclaimed from its bot. Teams, ownership,
map, entities, stocks, RNG and rule settings are retained. No wire or checksum
layout changes; the other Palms still use the existing disconnect rules.
`NetOffline` closes the transport before any continuation or replacement alert.

The saved-game state code is in `src/savegame.c`. Version 2 retains the old
8-byte header and unchanged GameState bytes, with one viewer-player byte after
the state. Version 1 saves of the current GameState size still load as player
zero. Saving writes version 2. `SaveAllow(false)` protects both state and map
from writes and deletion after the user keeps an existing save. New game and
Resume reset that guard. App stop never asks to replace a network game's old
single-player save; it saves an offline copy only if no save exists.
