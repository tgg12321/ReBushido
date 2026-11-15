# Playing it

What a player needs to know about ReBushido, on a PlayStation and in the
native port. How the program is built and what it is made of are in
[REBUSHIDO.md](REBUSHIDO.md) and [PORT.md](PORT.md).

This page is new and holds only what is settled. Everything the native
port cannot do yet is in [PORT.md](PORT.md), "What is left".

## Where the saves are

ReBushido saves what the game saves, in the same way: **memory card
files**, written in the PlayStation's own card format. There are two of
them, and the game never touches ReBushido's:

| The file | What it holds |
|---|---|
| `BASLUS-00663BUSHIDO2` | the game's own save: the fighters unlocked, chanbara's best times and the game's options. One block |
| `BASLUS-00663REBUSHI` | ReBushido's settings, which the options page sets. One block |

**On a PlayStation, and in an emulator**, they are on the memory card in
slot 1, as any save is.

**In the native port**, each slot is a file on the computer: an ordinary
raw memory card image of 128 KB, the same thing an emulator's card file
is. So a card tool or an emulator opens the port's card, and a card file
from an emulator works in the port.

- Slot 1 is `card1.mcd` and slot 2 is `card2.mcd`, **beside the
  program**, unless `--card1 FILE` or `--card2 FILE` says otherwise.
- A file that is not there is made the first time the game looks at that
  slot: formatted and empty, as a new card is.
- `--no-cards` leaves both slots empty, as a console with no card in it.
- What the game has been told is saved is on the disc before the message
  goes away: every write goes through to the file at once.

`tools/card.py` lists, checks and opens up a card image, whoever wrote it
(`python tools/card.py list card1.mcd`).

The window build is the same program with a window: the cards are the same
files in the same place, and nothing of this changes.
