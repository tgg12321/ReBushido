# Contributing

Thank you for looking. This is a single-person project and the maintainer
decides what goes in, so please open an issue before doing a large piece of
work.

## The two lines of work

- **`main` is the decompilation.** It must always build the original
  executable byte for byte. A change here is a better name, a clearer
  comment, a truer type, a correction to the documentation, or C that
  matches where a stand-in was. It never changes what the program does:
  the original's bugs stay, marked in comments and listed in
  `docs/STATUS.md`.
- **`rebushido` is the mod.** Bug fixes, enhancements and new features go
  there. It does not have to match the original.

## Before you open a pull request

- `wsl bash tools/build.sh checks` (or `make checks` inside WSL or Linux)
  must print `OK: all 7 checks pass`. GitHub Actions runs the same checks.
- Follow `docs/STYLE.md`; `python tools/format.py <files>` applies the
  layout.
- One subject per pull request, with a plain description of what changed
  and why.
- Do not add game assets, disc images, or code copied from another
  project's source tree.

## Licence

By contributing you agree that your contribution is licensed under the
project's licence (AGPL-3.0-or-later, see `LICENSE` and `NOTICE`).
