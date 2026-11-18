"""Runs the emulator for tools/playtest.py so that nothing of it can be seen
or left behind.

On Windows the program is started
  - on a desktop of its own (CreateDesktop): a window or a dialog it or the
    system opens for it is made there, where nobody looks, and never on the
    desktop of whoever is at the machine;
  - with the system's error boxes off (SetErrorMode), which it hands to its
    children;
  - in a job that ends every process in it when the job is closed, so that
    stopping the run takes the emulator's helpers with it, whatever became
    of this program.
While it runs its top-level windows on the user's desktop are looked for,
and any that is visible is reported: there should be none.

Elsewhere it is a plain child in a session of its own, ended as a group.

run() returns a Result. It never leaves a process: when it returns, the
job (or the group) has been ended and `left` says how many processes were
still in it after that, which should be 0. Several runs may be going at
once, each from a thread of its own (tools/playtest.py -j): each has its
own job, and nothing of one is shared with another but the hidden desktop.
"""
import os
import subprocess
import threading
import time

# Starting a process sets the error mode of this whole program for a
# moment: one thread at a time.
_START = threading.Lock()
# The hidden desktop's handle, once it is made (_run_windows).
_DESKTOP = []


class Result:
    def __init__(self):
        self.code = None      # the exit code; None if it had to be ended
        self.seconds = 0.0
        self.timed_out = False
        self.stopped = None   # what `watch` returned, if it ended the run
        self.windows = []     # titles of visible windows on the user's desktop
        self.left = 0         # processes still alive after the end
        self.pid = None       # the emulator's process id


def emulators_running():
    """The ids of emulator processes on this machine, whoever started them."""
    names = ("pcsx-redux.main", "pcsx-redux.exe", "pcsx-redux")
    found = []
    if os.name == "nt":
        out = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = [p.strip('"') for p in line.split('","')]
            if len(parts) > 1 and parts[0].lower() in names:
                found.append(int(parts[1]))
    else:
        out = subprocess.run(["ps", "-eo", "pid=,comm="], capture_output=True, text=True).stdout
        for line in out.splitlines():
            pid, _, name = line.strip().partition(" ")
            if name.strip() in names or name.strip() == "pcsx-redux.mai":
                found.append(int(pid))
    return found


def alive(pids):
    """Those of `pids` that are still emulator processes."""
    running = set(emulators_running())
    return [pid for pid in pids if pid in running]


def run(cmd, cwd, log_path, timeout, watch=None):
    """Runs cmd with its output in the file log_path. `watch`, called a few
    times a second, ends the run by returning something true."""
    if os.name == "nt":
        return _run_windows(cmd, cwd, log_path, timeout, watch)
    return _run_posix(cmd, cwd, log_path, timeout, watch)


def _run_posix(cmd, cwd, log_path, timeout, watch):
    import signal
    result = Result()
    env = dict(os.environ)
    for name in ("DISPLAY", "WAYLAND_DISPLAY"):  # nowhere to open a window
        env.pop(name, None)
    start = time.time()
    with open(log_path, "wb") as log:
        proc = subprocess.Popen(cmd, cwd=cwd, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                env=env, start_new_session=True)
        result.pid = proc.pid
        try:
            while proc.poll() is None:
                time.sleep(0.2)
                if watch:
                    result.stopped = watch()
                    if result.stopped:
                        break
                if time.time() - start > timeout:
                    result.timed_out = True
                    break
            result.code = proc.poll()
        finally:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except OSError:
                pass
            proc.wait()
    result.seconds = time.time() - start
    try:
        os.killpg(proc.pid, 0)
        result.left = 1
    except OSError:
        result.left = 0
    return result


def _run_windows(cmd, cwd, log_path, timeout, watch):
    import ctypes
    import ctypes.wintypes as wt

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    user32 = ctypes.WinDLL("user32", use_last_error=True)

    DESKTOP = "rebushido-playtest"
    GENERIC_ALL, GENERIC_READ, GENERIC_WRITE = 0x10000000, 0x80000000, 0x40000000
    SHARE_READ_WRITE, CREATE_ALWAYS, OPEN_EXISTING = 3, 2, 3
    CREATE_NO_WINDOW, CREATE_SUSPENDED = 0x08000000, 0x00000004
    STARTF_USESHOWWINDOW, STARTF_USESTDHANDLES, SW_HIDE = 0x1, 0x100, 0
    JOB_EXTENDED_LIMIT, JOB_BASIC_ACCOUNTING = 9, 1
    JOB_KILL_ON_CLOSE = 0x2000
    # no box for a critical error, a fault or a file not found
    ERROR_MODE = 0x0001 | 0x0002 | 0x8000
    WAIT_TIMEOUT = 0x102
    HANDLE_FLAG_INHERIT = 0x1
    EXTENDED_STARTUPINFO_PRESENT = 0x00080000
    PROC_THREAD_ATTRIBUTE_HANDLE_LIST = 0x00020002
    INVALID_HANDLE = wt.HANDLE(-1).value

    class StartupInfo(ctypes.Structure):
        _fields_ = [("cb", wt.DWORD), ("lpReserved", wt.LPWSTR), ("lpDesktop", wt.LPWSTR),
                    ("lpTitle", wt.LPWSTR), ("dwX", wt.DWORD), ("dwY", wt.DWORD),
                    ("dwXSize", wt.DWORD), ("dwYSize", wt.DWORD), ("dwXCountChars", wt.DWORD),
                    ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD), ("dwFlags", wt.DWORD),
                    ("wShowWindow", wt.WORD), ("cbReserved2", wt.WORD), ("lpReserved2", ctypes.c_void_p),
                    ("hStdInput", wt.HANDLE), ("hStdOutput", wt.HANDLE), ("hStdError", wt.HANDLE)]

    class StartupInfoEx(ctypes.Structure):
        _fields_ = [("StartupInfo", StartupInfo), ("lpAttributeList", ctypes.c_void_p)]

    class ProcessInformation(ctypes.Structure):
        _fields_ = [("hProcess", wt.HANDLE), ("hThread", wt.HANDLE),
                    ("dwProcessId", wt.DWORD), ("dwThreadId", wt.DWORD)]

    class SecurityAttributes(ctypes.Structure):
        _fields_ = [("nLength", wt.DWORD), ("lpSecurityDescriptor", ctypes.c_void_p),
                    ("bInheritHandle", wt.BOOL)]

    class BasicLimit(ctypes.Structure):
        _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                    ("LimitFlags", wt.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                    ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wt.DWORD),
                    ("Affinity", ctypes.c_size_t), ("PriorityClass", wt.DWORD),
                    ("SchedulingClass", wt.DWORD)]

    class IoCounters(ctypes.Structure):
        _fields_ = [(name, ctypes.c_uint64) for name in ("a", "b", "c", "d", "e", "f")]

    class ExtendedLimit(ctypes.Structure):
        _fields_ = [("BasicLimitInformation", BasicLimit), ("IoInfo", IoCounters),
                    ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                    ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]

    class BasicAccounting(ctypes.Structure):
        _fields_ = [("TotalUserTime", ctypes.c_int64), ("TotalKernelTime", ctypes.c_int64),
                    ("ThisPeriodTotalUserTime", ctypes.c_int64), ("ThisPeriodTotalKernelTime", ctypes.c_int64),
                    ("TotalPageFaultCount", wt.DWORD), ("TotalProcesses", wt.DWORD),
                    ("ActiveProcesses", wt.DWORD), ("TotalTerminatedProcesses", wt.DWORD)]

    enum_proc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    for function, restype, argtypes in (
        (user32.CreateDesktopW, wt.HANDLE,
         [wt.LPCWSTR, wt.LPCWSTR, ctypes.c_void_p, wt.DWORD, wt.DWORD, ctypes.c_void_p]),
        (user32.CloseDesktop, wt.BOOL, [wt.HANDLE]),
        (user32.EnumWindows, wt.BOOL, [enum_proc, wt.LPARAM]),
        (user32.GetWindowThreadProcessId, wt.DWORD, [wt.HWND, ctypes.POINTER(wt.DWORD)]),
        (user32.IsWindowVisible, wt.BOOL, [wt.HWND]),
        (user32.GetWindowTextW, ctypes.c_int, [wt.HWND, wt.LPWSTR, ctypes.c_int]),
        (kernel32.CreateJobObjectW, wt.HANDLE, [ctypes.c_void_p, wt.LPCWSTR]),
        (kernel32.SetInformationJobObject, wt.BOOL, [wt.HANDLE, ctypes.c_int, ctypes.c_void_p, wt.DWORD]),
        (kernel32.QueryInformationJobObject, wt.BOOL,
         [wt.HANDLE, ctypes.c_int, ctypes.c_void_p, wt.DWORD, ctypes.c_void_p]),
        (kernel32.AssignProcessToJobObject, wt.BOOL, [wt.HANDLE, wt.HANDLE]),
        (kernel32.TerminateJobObject, wt.BOOL, [wt.HANDLE, wt.UINT]),
        (kernel32.CreateProcessW, wt.BOOL,
         [wt.LPCWSTR, wt.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wt.BOOL, wt.DWORD, ctypes.c_void_p,
          wt.LPCWSTR, ctypes.c_void_p, ctypes.c_void_p]),
        (kernel32.CreateFileW, wt.HANDLE,
         [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p, wt.DWORD, wt.DWORD, wt.HANDLE]),
        (kernel32.ResumeThread, wt.DWORD, [wt.HANDLE]),
        (kernel32.WaitForSingleObject, wt.DWORD, [wt.HANDLE, wt.DWORD]),
        (kernel32.GetExitCodeProcess, wt.BOOL, [wt.HANDLE, ctypes.POINTER(wt.DWORD)]),
        (kernel32.CloseHandle, wt.BOOL, [wt.HANDLE]),
        (kernel32.SetErrorMode, wt.UINT, [wt.UINT]),
        (kernel32.SetHandleInformation, wt.BOOL, [wt.HANDLE, wt.DWORD, wt.DWORD]),
        (kernel32.InitializeProcThreadAttributeList, wt.BOOL,
         [ctypes.c_void_p, wt.DWORD, wt.DWORD, ctypes.POINTER(ctypes.c_size_t)]),
        (kernel32.UpdateProcThreadAttribute, wt.BOOL,
         [ctypes.c_void_p, wt.DWORD, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
          ctypes.c_void_p]),
        (kernel32.DeleteProcThreadAttributeList, None, [ctypes.c_void_p]),
    ):
        function.restype, function.argtypes = restype, argtypes

    def check(ok, what):
        if not ok or ok == INVALID_HANDLE:
            raise OSError("%s failed (Windows error %d)" % (what, ctypes.get_last_error()))
        return ok

    def visible_windows(pid):
        titles = []

        def each(hwnd, _):
            owner = wt.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value == pid and user32.IsWindowVisible(hwnd):
                title = ctypes.create_unicode_buffer(200)
                user32.GetWindowTextW(hwnd, title, 200)
                titles.append(title.value or "(no title)")
            return True

        user32.EnumWindows(enum_proc(each), 0)  # the desktop this program is on: the user's
        return titles

    result = Result()
    handles = []
    # One hidden desktop for all of this program's runs, made by the first
    # and held until the program ends: a desktop goes when its last handle
    # is closed, and one run's closing it as another opens it by its name
    # fails the second (access denied).
    with _START:
        if not _DESKTOP:
            _DESKTOP.append(check(user32.CreateDesktopW(DESKTOP, None, None, 0, GENERIC_ALL, None),
                                  "CreateDesktop"))
    try:
        job = check(kernel32.CreateJobObjectW(None, None), "CreateJobObject")
        handles.append(job)
        limits = ExtendedLimit()
        limits.BasicLimitInformation.LimitFlags = JOB_KILL_ON_CLOSE
        check(kernel32.SetInformationJobObject(job, JOB_EXTENDED_LIMIT, ctypes.byref(limits),
                                               ctypes.sizeof(limits)), "SetInformationJobObject")
        startup_ex = StartupInfoEx()
        startup = startup_ex.StartupInfo
        startup.cb = ctypes.sizeof(startup_ex)
        startup.lpDesktop = "WinSta0\\" + DESKTOP
        startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW
        startup.wShowWindow = SW_HIDE
        process = ProcessInformation()
        line = ctypes.create_unicode_buffer(subprocess.list2cmdline(cmd))
        # The run's output and its empty input are handed down to the
        # emulator, and nothing else of this program's: other threads may
        # be starting other runs, or a program whose output is a pipe this
        # one reads, and an emulator that held such a handle would keep
        # that pipe open until it ended.
        inherit = SecurityAttributes(ctypes.sizeof(SecurityAttributes), None, True)
        log = check(kernel32.CreateFileW(log_path, GENERIC_WRITE, SHARE_READ_WRITE, ctypes.byref(inherit),
                                         CREATE_ALWAYS, 0x80, None), "opening " + log_path)
        handles.append(log)
        nul = check(kernel32.CreateFileW("NUL", GENERIC_READ, SHARE_READ_WRITE, ctypes.byref(inherit),
                                         OPEN_EXISTING, 0, None), "opening NUL")
        handles.append(nul)
        startup.hStdInput, startup.hStdOutput, startup.hStdError = nul, log, log
        size = ctypes.c_size_t()
        kernel32.InitializeProcThreadAttributeList(None, 1, 0, ctypes.byref(size))
        attributes = ctypes.create_string_buffer(size.value)
        check(kernel32.InitializeProcThreadAttributeList(attributes, 1, 0, ctypes.byref(size)),
              "InitializeProcThreadAttributeList")
        inherited = (wt.HANDLE * 2)(log, nul)
        try:
            check(kernel32.UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                                     ctypes.sizeof(inherited), None, None),
                  "UpdateProcThreadAttribute")
            startup_ex.lpAttributeList = ctypes.cast(attributes, ctypes.c_void_p)
            # the error mode is the whole program's: one thread at a time
            with _START:
                old_mode = kernel32.SetErrorMode(ERROR_MODE)
                try:
                    # Suspended, so that it is in the job before it can start a child.
                    check(kernel32.CreateProcessW(None, line, None, None, True,
                                                  CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                                                  None, cwd, ctypes.byref(startup_ex), ctypes.byref(process)),
                          "starting " + cmd[0])
                finally:
                    kernel32.SetErrorMode(old_mode)
        finally:
            kernel32.DeleteProcThreadAttributeList(attributes)
        handles += [process.hThread, process.hProcess]
        result.pid = process.dwProcessId

        start = time.time()
        try:
            check(kernel32.AssignProcessToJobObject(job, process.hProcess), "AssignProcessToJobObject")
            kernel32.ResumeThread(process.hThread)
            while kernel32.WaitForSingleObject(process.hProcess, 200) == WAIT_TIMEOUT:
                for title in visible_windows(process.dwProcessId):
                    if title not in result.windows:
                        result.windows.append(title)
                if watch:
                    result.stopped = watch()
                    if result.stopped:
                        break
                if time.time() - start > timeout:
                    result.timed_out = True
                    break
            else:
                code = wt.DWORD()
                kernel32.GetExitCodeProcess(process.hProcess, ctypes.byref(code))
                result.code = code.value
        finally:
            # Every process of the job, the emulator's crash reporter with it.
            kernel32.TerminateJobObject(job, 1)
            kernel32.WaitForSingleObject(process.hProcess, 10000)
            for _ in range(50):
                accounting = BasicAccounting()
                kernel32.QueryInformationJobObject(job, JOB_BASIC_ACCOUNTING, ctypes.byref(accounting),
                                                   ctypes.sizeof(accounting), None)
                result.left = accounting.ActiveProcesses
                if result.left == 0:
                    break
                time.sleep(0.1)
            result.seconds = time.time() - start
    finally:
        for handle in reversed(handles):
            kernel32.CloseHandle(handle)
    return result
