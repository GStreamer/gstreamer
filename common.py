import os
import sys
import shlex
import shutil
import argparse
import platform
import subprocess


ROOTDIR = os.path.abspath(os.path.dirname(__file__))


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



def git(*args, repository_path='.'):
    return subprocess.check_output(["git"] + list(args), cwd=repository_path,
                                   stdin=subprocess.DEVNULL,
                                   stderr=subprocess.STDOUT).decode()

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
