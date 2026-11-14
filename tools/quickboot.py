#!/usr/bin/env python3
"""The fight a quick-boot build starts (docs/REBUSHIDO.md, "Booting straight
into a fight"): checks the Makefile's QB_ variables against the game's own
names, and lists what they can be.

    python3 tools/quickboot.py --list
    python3 tools/quickboot.py check QB_MODE=FIGHT_MODE_VS QB_P1=ROSTER_KANNUKI ...
    python3 tools/quickboot.py write build/play/QuickBoot.txt QB_MODE=... ...

`make quickboot` runs `check` before it compiles, so that a name the game
does not have is said here, with the names it does have, and not by the
compiler. `make quickboot-disc` runs `write`: the settings a line each,
beside the disc image, where tools/playtest.py reads what the disc was
built to start.

The values are members of the game's enums, read from the headers each
time, so a name added or renamed there is followed.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The modes that start from nothing but g_fight_config, as the original's
# debug menu starts them. A team battle and a link fight need what their
# own menus set up.
MODES = ("FIGHT_MODE_VS", "FIGHT_MODE_VSCPU", "FIGHT_MODE_STORY", "FIGHT_MODE_PRACTICE",
         "FIGHT_MODE_CHANBARA")
# variable: (what it is, the header, the enum, the members left out)
ENUMS = {
    "QB_MODE": ("the mode", "include/fight.h", "FightMode", None),
    "QB_P1": ("the first fighter", "include/fighter.h", "RosterId", ("ROSTER_COUNT",)),
    "QB_P2": ("the second fighter", "include/fighter.h", "RosterId", ("ROSTER_COUNT",)),
    "QB_WEAPON1": ("the first fighter's weapon", "include/weapon.h", "WeaponId", ("WEAPON_ID_COUNT",)),
    "QB_WEAPON2": ("the second fighter's weapon", "include/weapon.h", "WeaponId", ("WEAPON_ID_COUNT",)),
    "QB_STAGE": ("the stage", "include/stage.h", "StageIndex", ()),
}
ORDER = ("QB_MODE", "QB_P1", "QB_P2", "QB_WEAPON1", "QB_WEAPON2", "QB_STAGE", "QB_CPU2")
DISC_NAME = re.compile(r"QuickBoot[\w-]*$")


def members(header, enum):
    with open(os.path.join(REPO, header), encoding="utf-8") as f:
        text = re.sub(r"/\*.*?\*/", " ", f.read(), flags=re.S)
    m = re.search(r"\benum\s+%s\s*\{([^}]*)\}" % enum, text)
    if not m:
        sys.exit("quickboot.py: %s no longer has enum %s" % (header, enum))
    return [entry.split("=")[0].strip() for entry in m.group(1).split(",") if entry.strip()]


def legal(variable):
    if variable == "QB_CPU2":
        return ["0", "1"]
    if variable == "QB_MODE":
        return [name for name in members(*ENUMS[variable][1:3]) if name in MODES]
    _, header, enum, left_out = ENUMS[variable]
    return [name for name in members(header, enum) if name not in left_out]


def read(args):
    settings = {}
    for arg in args:
        name, _, value = arg.partition("=")
        settings[name] = value
    return settings


def check(args):
    settings = read(args)
    problems = []
    for name, value in settings.items():
        if name == "QB_NAME":
            if not DISC_NAME.match(value):
                problems.append("QB_NAME=%s: a quick-boot disc's name begins with QuickBoot "
                                "(QuickBoot, QuickBoot-lighthouse), so that it can replace no other disc" % value)
        elif name not in ORDER:
            problems.append("%s is not a quick-boot variable: they are %s and QB_NAME" % (name, ", ".join(ORDER)))
        elif value not in legal(name):
            problems.append("%s=%s: it is one of\n    %s" % (name, value, "\n    ".join(legal(name))))
    for problem in problems:
        print("quickboot.py: " + problem)
    return 1 if problems else 0


def write(out, args):
    settings = read(args)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# What this quick-boot disc starts (make quickboot-disc).\n")
        for name in ORDER:
            if name in settings:
                f.write("%s=%s\n" % (name, settings[name]))
    return 0


def show():
    print("The variables of `make quickboot` and `make quickboot-disc`, and what each can be:\n")
    for name in ORDER:
        if name == "QB_CPU2":
            print("QB_CPU2: whether the CPU plays the second fighter\n    0  1\n")
            continue
        print("%s: %s (enum %s, %s)" % (name, ENUMS[name][0], ENUMS[name][2], ENUMS[name][1]))
        for value in legal(name):
            print("    " + value)
        print()
    print("QB_NAME: the disc image's name in build/play/, beginning with QuickBoot")
    return 0


def main():
    args = sys.argv[1:]
    if args[:1] == ["--list"]:
        return show()
    if args[:1] == ["check"]:
        return check(args[1:])
    if args[:1] == ["write"] and len(args) > 1:
        return write(args[1], args[2:])
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main())
