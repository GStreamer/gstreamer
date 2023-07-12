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
#  this program.  If not, see <http://www.gnu.org/licenses/>.

"""GStreamer Development Utilities Common Main module."""

import sys
import os
import traceback
from operator import attrgetter
import logging
import locale
import gettext
from gettext import gettext as _, ngettext

import gi

from gi.repository import GLib
from gi.repository import GObject
from gi.repository import Gtk


class ExceptionHandler (object):

    exc_types = (Exception,)
    priority = 50
    inherit_fork = True

    _handling_exception = False

    def __call__(self, exc_type, exc_value, exc_traceback):

        raise NotImplementedError(
            "derived classes need to override this method")


class DefaultExceptionHandler (ExceptionHandler):
    exc_types = (BaseException,)
    priority = 0
    inherit_fork = True

    def __init__(self, excepthook):

        ExceptionHandler.__init__(self)

        self.excepthook = excepthook

    def __call__(self, *exc_info):

        return self.excepthook(*exc_info)


class ExitOnInterruptExceptionHandler (ExceptionHandler):

    exc_types = (KeyboardInterrupt,)
    priority = 100
    inherit_fork = False

    exit_status = 2

    def __call__(self, *args):

        print("Interrupt caught, exiting.", file=sys.stderr)

        sys.exit(self.exit_status)


class MainLoopWrapper (ExceptionHandler):

    priority = 95
    inherit_fork = False

    def __init__(self, enter, exit):

        ExceptionHandler.__init__(self)

        self.exc_info = (None,) * 3
        self.enter = enter
        self.exit = exit

    def __call__(self, *exc_info):

        self.exc_info = exc_info
        self.exit()

    def run(self):

        ExceptHookManager.register_handler(self)
        try:
            self.enter()
        finally:
            ExceptHookManager.unregister_handler(self)

        if self.exc_info != (None,) * 3:
            # Re-raise unhandled exception that occured while running the loop.
            exc_type, exc_value, exc_tb = self.exc_info
            raise exc_value


class ExceptHookManagerClass (object):

    def __init__(self):

        self._in_forked_child = False

        self.handlers = []

    def setup(self):

        if sys.excepthook == self.__excepthook:
            raise ValueError("already set up")

        hook = sys.excepthook
        self.__instrument_excepthook()
        self.__instrument_fork()
        self.register_handler(DefaultExceptionHandler(hook))

    def shutdown(self):

        if sys.excepthook != self.__excepthook:
            raise ValueError("not set up")

        self.__restore_excepthook()
        self.__restore_fork()

    def __instrument_excepthook(self):

        hook = sys.excepthook
        self._original_excepthook = hook
        sys.excepthook = self.__excepthook

    def __restore_excepthook(self):

        sys.excepthook = self._original_excepthook

    def __instrument_fork(self):

        try:
            fork = os.fork
        except AttributeError:
            # System has no fork() system call.
            self._original_fork = None
        else:
            self._original_fork = fork
            os.fork = self.__fork

    def __restore_fork(self):

        if not hasattr(os, "fork"):
            return

        os.fork = self._original_fork

    def entered_forked_child(self):

        self._in_forked_child = True

        for handler in tuple(self.handlers):
            if not handler.inherit_fork:
                self.handlers.remove(handler)

    def register_handler(self, handler):

        if self._in_forked_child and not handler.inherit_fork:
            return

        self.handlers.append(handler)

    def unregister_handler(self, handler):

        self.handlers.remove(handler)

    def __fork(self):

        pid = self._original_fork()
        if pid == 0:
            # Child process.
            self.entered_forked_child()
        return pid

    def __excepthook(self, exc_type, exc_value, exc_traceback):

        for handler in sorted(self.handlers,
                              key=attrgetter("priority"),
                              reverse=True):

            if handler._handling_exception:
                continue

            for type_ in handler.exc_types:
                if issubclass(exc_type, type_):
                    break
            else:
                continue

            handler._handling_exception = True
            handler(exc_type, exc_value, exc_traceback)
            # Not using try...finally on purpose here.  If the handler itself
            # fails with an exception, this prevents recursing into it again.
            handler._handling_exception = False
            return

        else:
            from warnings import warn
            warn("ExceptHookManager: unhandled %r" % (exc_value,),
                 RuntimeWarning,
                 stacklevel=2)


ExceptHookManager = ExceptHookManagerClass()


class PathsBase (object):

    data_dir = None
    icon_dir = None
    locale_dir = None

    @classmethod
    def setup_installed(cls, data_prefix):
        """Set up paths for running from a regular installation."""

        pass

    @classmethod
    def setup_devenv(cls, source_dir):
        """Set up paths for running the development environment
        (i.e. directly from the source dist)."""

        pass

    @classmethod
    def ensure_setup(cls):
        """If paths are still not set up, try to set from a fallback."""

        if cls.data_dir is None:
            source_dir = os.path.dirname(
                os.path.dirname(os.path.abspath(__file__)))
            cls.setup_devenv(source_dir)

    def __new__(cls):

        raise RuntimeError("do not create instances of this class -- "
                           "use the class object directly")


class PathsProgramBase (PathsBase):

    program_name = None

    @classmethod
    def setup_installed(cls, data_prefix):

        if cls.program_name is None:
            raise NotImplementedError(
                "derived classes need to set program_name attribute")

        cls.data_dir = os.path.join(data_prefix, cls.program_name)
        cls.icon_dir = os.path.join(data_prefix, "icons")
        cls.locale_dir = os.path.join(data_prefix, "locale")

    @classmethod
    def setup_devenv(cls, source_dir):
        """Set up paths for running the development environment
        (i.e. directly from the source dist)."""

        # This is essential: The GUI module needs to find the .glade file.
        cls.data_dir = os.path.join(source_dir, "data")

        # The locale data might be missing if "setup.py build" wasn't run.
        cls.locale_dir = os.path.join(source_dir, "build", "mo")

        # Not setting icon_dir.  It is not useful since we don't employ the
        # needed directory structure in the source dist.


def _init_excepthooks():

    ExceptHookManager.setup()
    ExceptHookManager.register_handler(ExitOnInterruptExceptionHandler())


def _init_paths(paths):

    paths.ensure_setup()


def _init_locale(gettext_domain=None):

    if Paths.locale_dir and gettext_domain is not None:
        try:
            locale.setlocale(locale.LC_ALL, "")
        except locale.Error as exc:
            from warnings import warn
            warn("locale error: %s" % (exc,),
                 RuntimeWarning,
                 stacklevel=2)
            Paths.locale_dir = None
        else:
            gettext.bindtextdomain(gettext_domain, Paths.locale_dir)
            gettext.textdomain(gettext_domain)


def _init_logging(level):
    if level == "none":
        return

    mapping = {"debug": logging.DEBUG,
               "info": logging.INFO,
               "warning": logging.WARNING,
               "error": logging.ERROR,
               "critical": logging.CRITICAL}
    logging.basicConfig(level=mapping[level],
                        format='%(asctime)s.%(msecs)03d %(levelname)8s %(name)20s: %(message)s',
                        datefmt='%H:%M:%S')

    logger = logging.getLogger("main")
    logger.debug("logging at level %s", logging.getLevelName(level))
    logger.info("using Python %i.%i.%i %s %i", *sys.version_info)


def _init_log_option(parser):
    choices = ["none", "debug", "info", "warning", "error", "critical"]
    parser.add_option("--log-level", "-l",
                      type="choice",
                      choices=choices,
                      action="store",
                      dest="log_level",
                      default="none",
                      help=_("Enable logging, possible values: ") + ", ".join(choices))
    return parser


def main(main_function, option_parser, gettext_domain=None, paths=None):

    # FIXME:
    global Paths
    Paths = paths

    _init_excepthooks()
    _init_paths(paths)
    _init_locale(gettext_domain)
    parser = _init_log_option(option_parser)
    options, args = option_parser.parse_args()
    _init_logging(options.log_level)

    try:
        main_function(args)
    finally:
        logging.shutdown()
