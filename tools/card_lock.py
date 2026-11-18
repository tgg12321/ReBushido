#!/usr/bin/env python3
"""One lock on the machine's graphics card, shared by every checkout
(BUILD.md, "The gate"):

    python tools/card_lock.py                  who would wait, and why

A timing run (tools/port_bench.py, tools/port_budget.py) holds it alone
for the whole of its run; a picture prover (tools/port_gl_prove.py and
those that use its `--gpu`: on the card, or on llvmpipe, which keeps
every processor busy) holds it beside other provers, so that none of
them runs while a bench measures, and a bench waits until they are done.
What else loads the machine (play tests, builds) takes no lock: the bench
records it (`load`) and warns. A run that has to wait says so,
and goes on when the lock is free.

The lock is the file `rebushido-card.lock` in the main checkout's git
folder (`git rev-parse --git-common-dir`), opened by Windows: alone with
nothing shared for a timing run, and for reading, with reading shared,
for a prover. Windows lets go of a file a process held when the process
ends however it ends, so a run that crashes holds nothing. In WSL a
Windows PowerShell is started that opens the file and holds it while
this process lives (its input is this process's pipe: when this ends,
the pipe closes and it ends too); WSL's own locks on a Windows folder
are not seen by Windows. Where there is neither Windows nor WSL, nothing
is locked.

A process that holds the lock sets REBUSHIDO_CARD_LOCK in its
environment, so that a run it starts does not wait for its parent.
"""
import base64
import os
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))

ENV = 'REBUSHIDO_CARD_LOCK'
NAME = 'rebushido-card.lock'
# How often a waiting run tries again.
RETRY_SECONDS = 2
# The lock held by this process: kept so that it lives as long as this does.
_HELD = []


def lock_path():
    import port_budget

    main = port_budget.main_checkout()
    git = os.path.join(main, '.git')
    return os.path.join(git if os.path.isdir(git) else main, NAME)


def _windows_open(path, alone):
    """The file opened by Windows' own call, or None while it is held
    otherwise."""
    import ctypes
    from ctypes import wintypes

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                                   wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    generic_read, generic_write = 0x80000000, 0x40000000
    share_read = 0x1
    open_always = 4
    handle = kernel.CreateFileW(path, generic_read | (generic_write if alone else 0),
                                0 if alone else share_read, None, open_always, 0x80, None)
    if handle in (None, wintypes.HANDLE(-1).value):
        return None
    return handle


# What WSL starts: the same as _windows_open, in Windows PowerShell, then
# held until its input ends.
_HELPER = r'''
$path = '%s'
$access = '%s'
$share = '%s'
$said = $false
while ($true) {
    try {
        $f = [System.IO.File]::Open($path, 'OpenOrCreate', $access, $share)
        break
    } catch {
        if (-not $said) { [Console]::Out.WriteLine('waiting'); [Console]::Out.Flush(); $said = $true }
        Start-Sleep -Seconds %d
    }
}
[Console]::Out.WriteLine('held')
[Console]::Out.Flush()
[void][Console]::In.ReadToEnd()
$f.Close()
'''


def _waiting(what, why):
    print('card-lock: %s waits for the graphics card: %s (%s)' % (what, why, lock_path()), flush=True)


def hold(alone, what):
    """Waits for the lock, then holds it until this process ends. `alone`:
    a timing run; otherwise a prover, beside other provers."""
    if os.environ.get(ENV) or _HELD:
        return True
    why = ('another run is on the card (a bench, a budget or a prover of some checkout)' if alone
           else 'a bench or a budget of some checkout is timing')
    started = time.time()
    if os.name == 'nt':
        path = lock_path()
        said = False
        while True:
            handle = _windows_open(path, alone)
            if handle is not None:
                break
            if not said:
                _waiting(what, why)
                said = True
            time.sleep(RETRY_SECONDS)
        _HELD.append(handle)
    else:
        try:
            path = subprocess.run(['wslpath', '-w', lock_path()], capture_output=True, text=True,
                                  check=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            return False  # not WSL: there is no card shared with Windows to lock
        script = _HELPER % (path.replace("'", "''"), 'ReadWrite' if alone else 'Read',
                            'None' if alone else 'Read', RETRY_SECONDS)
        encoded = base64.b64encode(script.encode('utf-16-le')).decode('ascii')
        try:
            helper = subprocess.Popen(['powershell.exe', '-NoProfile', '-NonInteractive', '-EncodedCommand',
                                       encoded], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        except OSError:
            return False
        while True:
            line = helper.stdout.readline().strip()
            if line == 'waiting':
                _waiting(what, why)
            elif line == 'held':
                break
            elif not line and helper.poll() is not None:
                print('card-lock: the lock could not be taken (PowerShell ended); going on without it')
                return False
        _HELD.append(helper)
    if time.time() - started > RETRY_SECONDS:
        print('card-lock: %s has the card after %.0f s' % (what, time.time() - started), flush=True)
    os.environ[ENV] = 'alone' if alone else 'shared'
    return True


def main():
    print('card-lock: %s' % lock_path())
    if os.name != 'nt':
        print('card-lock: (asked from WSL; the lock is Windows\')')
        return 0
    for alone, what in ((True, 'a timing run'), (False, 'a prover on the card')):
        handle = _windows_open(lock_path(), alone)
        if handle is None:
            print('card-lock: %s would wait' % what)
        else:
            import ctypes

            ctypes.windll.kernel32.CloseHandle(handle)
            print('card-lock: %s would start now' % what)
    return 0


if __name__ == '__main__':
    sys.exit(main())
