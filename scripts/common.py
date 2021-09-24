import os
import sys
import shlex
import shutil
import argparse
import platform
import subprocess
import uuid


ROOTDIR = os.path.abspath(os.path.dirname(__file__))


if os.name == 'nt':
    import ctypes
    from ctypes import wintypes
    _GetShortPathNameW = ctypes.windll.kernel32.GetShortPathNameW
    _GetShortPathNameW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD]
    _GetShortPathNameW.restype = wintypes.DWORD

def win32_get_short_path_name(long_name):
    """
    Gets the short path name of a given long path.
    http://stackoverflow.com/a/23598461/200291
    """
    output_buf_size = 0
    while True:
        output_buf = ctypes.create_unicode_buffer(output_buf_size)
        needed = _GetShortPathNameW(long_name, output_buf, output_buf_size)
        if output_buf_size >= needed:
            return output_buf.value
        else:
            output_buf_size = needed


def get_wine_shortpath(winecmd, wine_paths):
    seen = set()
    wine_paths += [p for p in wine_paths if not (p in seen or seen.add(p))]

    getShortPathScript = '%s.bat' % str(uuid.uuid4()).lower()[:5]
    with open(getShortPathScript, mode='w') as f:
        f.write("@ECHO OFF\nfor %%x in (%*) do (\n echo|set /p=;%~sx\n)\n")
        f.flush()
    try:
        with open(os.devnull, 'w') as stderr:
            wine_path = subprocess.check_output(
                winecmd +
                ['cmd', '/C', getShortPathScript] + wine_paths,
                stderr=stderr).decode('utf-8')
    except subprocess.CalledProcessError as e:
        print("Could not get short paths: %s" % e)
        wine_path = ';'.join(wine_paths)
    finally:
        os.remove(getShortPathScript)
    if len(wine_path) > 2048:
        raise AssertionError('WINEPATH size {} > 2048'
                                ' this will cause random failure.'.format(
                                    len(wine_path)))
    return wine_path


class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'

    force_disable = False

    def _windows_ansi():
        from ctypes import windll, byref
        from ctypes.wintypes import DWORD

        kernel = windll.kernel32
        stdout = kernel.GetStdHandle(-11)
        mode = DWORD()
        if not kernel.GetConsoleMode(stdout, byref(mode)):
            return False
        # Try setting ENABLE_VIRTUAL_TERMINAL_PROCESSING (0x4)
        # If that fails (returns 0), we disable colors
        return kernel.SetConsoleMode(stdout, mode.value | 0x4) or os.environ.get('ANSICON')

    @classmethod
    def can_enable(cls):
        if not os.isatty(sys.stdout.fileno()):
            return False
        if platform.system().lower() == 'windows':
            return cls._windows_ansi()
        return os.environ.get('TERM') != 'dumb'

    @classmethod
    def disable(cls):
        cls.HEADER = ''
        cls.OKBLUE = ''
        cls.OKGREEN = ''
        cls.WARNING = ''
        cls.FAIL = ''
        cls.ENDC = ''

    @classmethod
    def enable(cls):
        if cls.force_disable:
            return

        cls.HEADER = '\033[95m'
        cls.OKBLUE = '\033[94m'
        cls.OKGREEN = '\033[92m'
        cls.WARNING = '\033[93m'
        cls.FAIL = '\033[91m'
        cls.ENDC = '\033[0m'



def git(*args, repository_path='.', fatal=True):
    try:
        ret = subprocess.check_output(["git"] + list(args), cwd=repository_path,
                                      stdin=subprocess.DEVNULL,
                                      stderr=subprocess.STDOUT).decode()
    except subprocess.CalledProcessError as e:
        if fatal:
            raise e
        print("Non-fatal error running git {}:\n{}".format(' '.join(args), e))
        return None
    return ret

def accept_command(commands):
    """Search @commands and returns the first found absolute path."""
    for command in commands:
        command = shutil.which(command)
        if command:
            return command
    return None

def get_meson():
    meson = os.path.join(ROOTDIR, 'meson', 'meson.py')
    if os.path.exists(meson):
        return [sys.executable, meson]

    mesonintrospect = os.environ.get('MESONINTROSPECT', '')
    for comp in shlex.split (mesonintrospect):
        # mesonintrospect might look like "/usr/bin/python /somewhere/meson introspect",
        # let's not get tricked
        if 'python' in os.path.basename (comp):
            continue
        if os.path.exists(comp):
            if comp.endswith('.py'):
                return [sys.executable, comp]
            else:
                return [comp]

    meson = accept_command(['meson.py'])
    if meson:
        return [sys.executable, meson]
    meson = accept_command(['meson'])
    if meson:
        return [meson]
    raise RuntimeError('Could not find Meson')
