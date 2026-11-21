#!/usr/bin/env python3
"""Format C sources with the repo's .clang-format (clang-format 18.1.8).

    python tools/format.py <file>...      format in place
    python tools/format.py --check <file>...   exit 1 if a file would change
    python tools/format.py [--check] --all     every C file git tracks under
                                               src/, include/, movovl/, tests/,
                                               tools/patcher/

clang-format only moves whitespace, so it cannot change the built bytes. The
Sony headers under include/psxsdk/ that are kept as shipped are skipped. Runs
clang-format from the WSL venv when started from Windows.
"""
import os
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AS_SHIPPED = {"inline_o.h", "inline_c.h", "gtemac.h", "libsn.h", "stb_truetype.h", "stb_vorbis.c", "stb_image.h",
              "monocypher.c", "monocypher.h", "monocypher-ed25519.c", "monocypher-ed25519.h"}


def main():
    args = sys.argv[1:]
    check = "--check" in args
    files = [a.replace("\\", "/") for a in args if not a.startswith("--")]
    if "--all" in args:
        listed = subprocess.run(
            ["git", "ls-files", "src", "include", "movovl", "tests", "tools/patcher"],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        if listed.returncode != 0:
            # (a worktree's git, from WSL, where its link to the checkout
            # is a Windows path): not a pass with nothing checked
            print("format.py: git ls-files failed: %s" % listed.stderr.strip())
            return 1
        files = listed.stdout.split()
    files = [f for f in files if os.path.basename(f) not in AS_SHIPPED and f.endswith((".c", ".h"))]
    if not files:
        return 0
    flags = ["--dry-run", "--Werror"] if check else ["-i"]
    if os.name == "nt":
        drive, rest = ROOT[0].lower(), ROOT[2:].replace("\\", "/")
        cmd = "cd %s && .venv/bin/clang-format %s %s" % (
            shlex.quote("/mnt/%s%s" % (drive, rest)),
            " ".join(flags),
            " ".join(shlex.quote(f) for f in files),
        )
        return subprocess.run(["wsl", "bash", "-c", cmd]).returncode
    return subprocess.run([os.path.join(ROOT, ".venv/bin/clang-format"), *flags, *files], cwd=ROOT).returncode


if __name__ == "__main__":
    sys.exit(main())
