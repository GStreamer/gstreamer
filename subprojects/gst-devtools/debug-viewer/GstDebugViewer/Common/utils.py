# -*- coding: utf-8; mode: python; -*-
#
#  GStreamer Development Utilities
#
#  Copyright (C) 2007 René Stadler <mail@renestadler.de>
#
#  This program is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by the Free
#  Software Foundation; either version 3 of the License, or (at your option)
#  any later version.
#
#  This program is distributed in the hope that it will be useful, but WITHOUT
#  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
#  more details.
#
#  You should have received a copy of the GNU General Public License along with
#  this program; if not, see <http://www.gnu.org/licenses/>.

"""GStreamer Development Utilities Common utils module."""

import os
import logging
import subprocess as _subprocess


class SingletonMeta (type):

    def __init__(cls, name, bases, dict_):

        from weakref import WeakValueDictionary

        super(SingletonMeta, cls).__init__(name, bases, dict_)

        cls._singleton_instances = WeakValueDictionary()

    def __call__(cls, *a, **kw):

        kw_key = tuple(sorted(kw.items()))

        try:
            obj = cls._singleton_instances[a + kw_key]
        except KeyError:
            obj = super(SingletonMeta, cls).__call__(*a, **kw)
            cls._singleton_instances[a + kw_key] = obj
        return obj


def gettext_cache():
    """Return a callable object that operates like gettext.gettext, but is much
    faster when a string is looked up more than once.  This is very useful in
    loops, where calling gettext.gettext can quickly become a major performance
    bottleneck."""

    from gettext import gettext

    d = {}

    def gettext_cache_access(s):

        if s not in d:
            d[s] = gettext(s)
        return d[s]

    return gettext_cache_access


class ClassProperty (property):

    "Like the property class, but also invokes the getter for class access."

    def __init__(self, fget=None, fset=None, fdel=None, doc=None):

        property.__init__(self, fget, fset, fdel, doc)

        self.__fget = fget

    def __get__(self, obj, obj_class=None):

        ret = property.__get__(self, obj, obj_class)
        if ret == self:
            return self.__fget(None)
        else:
            return ret


class _XDGClass (object):

    """Partial implementation of the XDG Base Directory specification v0.6.

    http://standards.freedesktop.org/basedir-spec/basedir-spec-0.6.html"""

    def __init__(self):

        self._add_base_dir("DATA_HOME", "~/.local/share")
        self._add_base_dir("CONFIG_HOME", "~/.config")
        self._add_base_dir("CACHE_HOME", "~/.cache")

    def _add_base_dir(self, name, default):

        dir = os.environ.get("XDG_%s" % (name,))
        if not dir:
            dir = os.path.expanduser(os.path.join(*default.split("/")))

        setattr(self, name, dir)


XDG = _XDGClass()


class SaveWriteFile (object):

    def __init__(self, filename, mode="wt"):

        from tempfile import mkstemp

        self.logger = logging.getLogger("tempfile")

        dir = os.path.dirname(filename)
        base_name = os.path.basename(filename)
        temp_prefix = "%s-tmp" % (base_name,)

        if dir:
            # Destination dir differs from current directory, ensure that it
            # exists:
            try:
                os.makedirs(dir)
            except OSError:
                pass

            self.clean_stale(dir, temp_prefix)

        fd, temp_name = mkstemp(dir=dir, prefix=temp_prefix)

        self.target_name = filename
        self.temp_name = temp_name
        self.real_file = os.fdopen(fd, mode)

    def __enter__(self):

        return self

    def __exit__(self, *exc_args):

        if exc_args == (None, None, None,):
            self.close()
        else:
            self.discard()

    def __del__(self):

        try:
            self.discard()
        except AttributeError:
            # If __init__ failed, self has no real_file attribute.
            pass

    def __close_real(self):

        if self.real_file:
            self.real_file.close()
            self.real_file = None

    def clean_stale(self, dir, temp_prefix):

        from time import time
        from glob import glob

        now = time()
        pattern = os.path.join(dir, "%s*" % (temp_prefix,))

        for temp_filename in glob(pattern):
            mtime = os.stat(temp_filename).st_mtime
            if now - mtime > 3600:
                self.logger.info("deleting stale temporary file %s",
                                 temp_filename)
                try:
                    os.unlink(temp_filename)
                except EnvironmentError as exc:
                    self.logger.warning("deleting stale temporary file "
                                        "failed: %s", exc)

    def tell(self, *a, **kw):

        return self.real_file.tell(*a, **kw)

    def write(self, *a, **kw):

        return self.real_file.write(*a, **kw)

    def close(self):

        self.__close_real()

        if self.temp_name:
            try:
                os.rename(self.temp_name, self.target_name)
            except OSError as exc:
                import errno
                if exc.errno == errno.EEXIST:
                    # We are probably on windows.
                    os.unlink(self.target_name)
                    os.rename(self.temp_name, self.target_name)
            self.temp_name = None

    def discard(self):

        self.__close_real()

        if self.temp_name:

            try:
                os.unlink(self.temp_name)
            except EnvironmentError as exc:
                self.logger.warning("deleting temporary file failed: %s", exc)
            self.temp_name = None


class TeeWriteFile (object):

    # TODO Py2.5: Add context manager methods.

    def __init__(self, *file_objects):

        self.files = list(file_objects)

    def close(self):

        for file in self.files:
            file.close()

    def flush(self):

        for file in self.files:
            file.flush()

    def write(self, string):

        for file in self.files:
            file.write(string)

    def writelines(self, lines):

        for file in self.files:
            file.writelines(lines)


class FixedPopen (_subprocess.Popen):

    def __init__(self, args, **kw):

        # Unconditionally specify all descriptors as redirected, to
        # work around Python bug #1358527 (which is triggered for
        # console-less applications on Windows).

        close = []

        for name in ("stdin", "stdout", "stderr",):
            target = kw.get(name)
            if not target:
                kw[name] = _subprocess.PIPE
                close.append(name)

        _subprocess.Popen.__init__(self, args, **kw)

        for name in close:
            fp = getattr(self, name)
            fp.close()
            setattr(self, name, None)


class DevhelpError (EnvironmentError):

    pass


class DevhelpUnavailableError (DevhelpError):

    pass


class DevhelpClient (object):

    def available(self):

        try:
            self.version()
        except DevhelpUnavailableError:
            return False
        else:
            return True

    def version(self):

        return self._invoke("--version")

    def search(self, entry):

        self._invoke_no_interact("-s", entry)

    def _check_os_error(self, exc):

        import errno
        if exc.errno == errno.ENOENT:
            raise DevhelpUnavailableError()

    def _invoke(self, *args):

        from subprocess import PIPE

        try:
            proc = FixedPopen(("devhelp",) + args,
                              stdout=PIPE)
        except OSError as exc:
            self._check_os_error(exc)
            raise

        out, err = proc.communicate()

        if proc.returncode is not None and proc.returncode != 0:
            raise DevhelpError("devhelp exited with status %i"
                               % (proc.returncode,))
        return out

    def _invoke_no_interact(self, *args):

        from subprocess import PIPE

        try:
            proc = FixedPopen(("devhelp",) + args)
        except OSError as exc:
            self._check_os_error(exc)
            raise
