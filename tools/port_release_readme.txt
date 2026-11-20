ReBushido for Windows, version {version} ({commit})
==================================================

WHAT IT IS
----------
ReBushido is Bushido Blade 2 (Square / Lightweight, 1998) as a program
for the PC: the game's own code, rebuilt from a matching decompilation,
running in a window with sound, played with the keyboard or with
controllers, with a sharper picture if you want one and online play.

Nothing of the game is in this package: no models, textures, sound,
movies or disc image. The program reads all of that from your own disc
image as it runs.

WHAT YOU NEED
-------------
- Your own image of the North American disc of Bushido Blade 2
  (SLUS-00663), as a .cue file and its .bin file: a Redump-style dump,
  for example "Bushido Blade 2 (USA).cue" with
  "Bushido Blade 2 (USA).bin".
- A 64-bit Windows 10 or 11.
- For the Remastered picture, a graphics card with OpenGL 4.5 (any
  card of the last ten years). Without one the program shows the
  PlayStation's own picture.

WHAT IS IN THE PACKAGE
----------------------
  rebushido.exe       the program
  SDL2.dll            the library it opens its window with
  rebushido.exe.sym   the program's names: with it a crash says where
  mods\               the mods it comes with: the English top menu,
                      the sharp menus, the stages' lights and a sample
                      arena
  LICENSE.txt, NOTICE.txt, licenses\   the licences

Unpack the folder anywhere you can write to (not Program Files): the
program keeps its settings, memory cards and logs beside itself.

STARTING IT
-----------
Put your .cue and .bin in the folder with rebushido.exe and
double-click rebushido.exe. If there is no image beside it, it asks for
one with a file dialog. Or, from a terminal:

  rebushido.exe --image "C:\Games\Bushido Blade 2 (USA).cue"

It starts filling the screen; F11 or Alt+Enter puts it in a window and
back. "rebushido.exe --help" lists everything else it can be told.

Windows may warn that the program is from an unknown publisher (it is
not signed): "More info", then "Run anyway".

THE KEYS
--------
                   Player one         Player two   A controller
  Directions       the arrow keys     I K J L      d-pad or left stick
  Cross, Circle    Z, X               G, H         A, B
  Square, Triangle A, S               T, Y         X, Y
  L1, R1           Q, W               R, U         the shoulder buttons
  L2, R2           1, 2               5, 6         the triggers
  Start, Select    Enter, Right Shift P, O         Start, Back

  F1          the settings menu (a controller's guide button too)
  F2          the Original or the Remastered picture, at once
  Escape      pause in a fight; back from a menu
  F11         full screen or a window (Alt+Enter too)
  F3          the frames-a-second counter
  F9          keep a report of a problem (a folder in logs\)
  F12         keep a picture of the window (screenshots\)

The first controller plugged in is player one's, the second player
two's. Close the window, or "Quit ReBushido" at the foot of the
settings menu, to end the program.

THE SETTINGS MENU
-----------------
F1 opens it anywhere (or Select on the options page, the top menu and
the pause menu). The arrow keys choose a line and change it; Enter
opens a line or binds a button; Escape goes back. In it are the
picture, the sound, the keys and buttons of both players, the game's
Bug fixes, the mods, and Play online. What you change is written to
rebushido.cfg beside the program as the menu closes. A line marked
"(after a restart)" waits for the next start.

The game saves to memory card files beside the program, card1.mcd and
card2.mcd: ordinary memory card images, which an emulator opens too.

======================================================================
PLAYING ONLINE
======================================================================
Two players on two PCs play versus over the internet or a home
network, with rollback netcode: your own presses count at once, and
the other player's are put right in a moment if the game guessed them
wrong. No VPN or other program is needed.

Both of you need the same release (this package) and the same disc.
To be sure before you start, each run:

  rebushido.exe --check-online

It prints a setup code such as P7-XXXX-XXXX-XXXX, and the Play online
page shows it too. Send each other your codes by message: if they are
the same, you can play. If they differ, the game says which part
differs (the program, the disc, or the mods that change how fights
play).

Open the settings menu (F1) and choose Play online.

  Hosting: choose "Host a room", give it a name and, if you like, a
  password. The program finds its way through your router by itself
  (it asks the router to open the way: UPnP, NAT-PMP or PCP) and waits
  for the other player.

  Joining: choose "Browse rooms" and pick the host's room, with its
  password if it has one. Rooms you cannot join (another version, or
  another setup) are shown greyed.

The lobby is a small server of the project's (its address is on the
page, "Lobby server"): it keeps the list of rooms and introduces the
two PCs to each other. It never sees anything of the game but the
datagrams it may pass on. Once you are introduced, the program tries
every way between your PCs at once and takes the best that works:
IPv6 directly, the host's router port it opened, a "hole punched"
through both routers, and in the last resort the lobby server passing
the game's datagrams on (a little more delay). The line at the top of
the window says which way you got and the round trip ("ping").

The first time you host or join, Windows Defender Firewall asks
whether rebushido.exe may use the network: allow it, on Private
networks, and on Public ones too if Windows has your network marked as
public. If you said no by mistake: Windows Security, Firewall and
network protection, "Allow an app through firewall".

Advanced: by address, with no lobby. The host chooses "Host by
address" on a port (7845 if you change nothing) and the other player
"Join by address" with the host's address and port, such as
203.0.113.7:7845 or [2001:db8::7]:7845. The host's program asks its
router to open the port, as a room's does; if the router will not,
forward UDP port 7845 to the host's PC by hand in the router's
settings. "Your address for the other player" on the page finds the
address to send. If neither router can be opened, both can still
punch through: each finds their own address on the page, sends it to
the other, the host types the other's into "The other player's
address", and both start at once. From a terminal:

  rebushido.exe --host 0.0.0.0:7845           (the host)
  rebushido.exe --join 203.0.113.7:7845       (the other player)

When a session starts, the host's memory cards and ReBushido settings
are used by both, and nothing of it is saved to either player's cards.
The game goes to versus character select, where you both choose at
once; the host picks the stage. After a fight the game offers a
rematch. There is no pause online, and the settings menu cannot be
opened during a session. A line at the top of the window shows who
you are, the ping and the rollbacks.

If you are turned away, the program says why on the screen: another
version, another disc, another setup (with the part that differs), a
wrong password, a full room, or a host that could not be reached.
======================================================================

IF SOMETHING IS WRONG
---------------------
Every session writes a log in the folder logs\ beside the program. If
the game stops by itself, a box says so and names the log: that file
is what to send. F9 keeps a report of a problem (the picture, the
log, the times of the last ten seconds and your rebushido.cfg) in one
folder of logs\: send that folder.

LICENCE
-------
ReBushido is free software under the GNU Affero General Public
License, version 3 or later (LICENSE.txt). Its source is at
https://github.com/tgg12321/ReBushido, commit {commit}.
Bushido Blade 2 itself belongs to its rights holders; this project is
not affiliated with or endorsed by Square Enix, Lightweight or Sony
Interactive Entertainment. What else is in the program, and under
which licences, is in NOTICE.txt and licenses\.
