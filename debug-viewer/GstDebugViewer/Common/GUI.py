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

"""GStreamer Development Utilities Common GUI module."""

import os

import logging

import gi

gi.require_version('Gtk', '3.0')
from gi.repository import GObject
from gi.repository import Gtk
from gi.repository import Gdk
from gi.types import GObjectMeta

import GstDebugViewer
from GstDebugViewer.Common import utils
from .generictreemodel import GenericTreeModel


def widget_add_popup_menu(widget, menu, button=3):

    def popup_callback(widget, event):

        if event.button == button:
            menu.popup(
                None, None, None, None, event.button, event.get_time())
        return False

    widget.connect("button-press-event", popup_callback)


class Actions (dict):

    def __init__(self):

        dict.__init__(self)

        self.groups = {}

    def __getattr__(self, name):

        try:
            return self[name]
        except KeyError:
            if "_" in name:
                try:
                    return self[name.replace("_", "-")]
                except KeyError:
                    pass

        raise AttributeError("no action with name %r" % (name,))

    def add_group(self, group):

        name = group.props.name
        if name in self.groups:
            raise ValueError("already have a group named %s", name)
        self.groups[name] = group
        for action in group.list_actions():
            self[action.props.name] = action


class Widgets (dict):

    def __init__(self, builder):

        widgets = (obj for obj in builder.get_objects()
                   if isinstance(obj, Gtk.Buildable))
        # Gtk.Widget.get_name() shadows out the GtkBuildable interface method
        # of the same name, hence calling the unbound interface method here:
        items = ((Gtk.Buildable.get_name(w), w,) for w in widgets)

        dict.__init__(self, items)

    def __getattr__(self, name):

        try:
            return self[name]
        except KeyError:
            if "_" in name:
                try:
                    return self[name.replace("_", "-")]
                except KeyError:
                    pass

        raise AttributeError("no widget with name %r" % (name,))


class WidgetFactory (object):

    def __init__(self, directory):

        self.directory = directory

    def get_builder(self, filename):

        builder_filename = os.path.join(self.directory, filename)

        builder = Gtk.Builder()
        builder.set_translation_domain(GstDebugViewer.GETTEXT_DOMAIN)
        builder.add_from_file(builder_filename)

        return builder

    def make(self, filename, widget_name, autoconnect=None):

        builder = self.get_builder(filename)

        if autoconnect is not None:
            builder.connect_signals(autoconnect)

        return Widgets(builder)

    def make_one(self, filename, widget_name):

        builder = self.get_builder(filename)

        return builder.get_object(widget_name)


class UIFactory (object):

    def __init__(self, ui_filename, actions=None):

        self.filename = ui_filename
        if actions:
            self.action_groups = actions.groups
        else:
            self.action_groups = ()

    def make(self, extra_actions=None):

        ui_manager = Gtk.UIManager()
        for action_group in list(self.action_groups.values()):
            ui_manager.insert_action_group(action_group, 0)
        if extra_actions:
            for action_group in extra_actions.groups:
                ui_manager.insert_action_group(action_group, 0)
        ui_manager.add_ui_from_file(self.filename)
        ui_manager.ensure_update()

        return ui_manager


class MetaModel (GObjectMeta):

    """Meta class for easy setup of gtk tree models.

    Looks for a class attribute named `columns' which must be set to a
    sequence of the form name1, type1, name2, type2, ..., where the
    names are strings.  This metaclass adds the following attributes
    to created classes:

    cls.column_types = (type1, type2, ...)
    cls.column_ids = (0, 1, ...)
    cls.name1 = 0
    cls.name2 = 1
    ...

    Example: A Gtk.ListStore derived model can use

        columns = ("COL_NAME", str, "COL_VALUE", str)

    and use this in __init__:

        GObject.GObject.__init__ (self, *self.column_types)

    Then insert data like this:

        self.set (self.append (),
                  self.COL_NAME, "spam",
                  self.COL_VALUE, "ham")
    """

    def __init__(cls, name, bases, dict):

        super(MetaModel, cls).__init__(name, bases, dict)

        spec = tuple(cls.columns)

        column_names = spec[::2]
        column_types = spec[1::2]
        column_indices = list(range(len(column_names)))

        for col_index, col_name, in zip(column_indices, column_names):
            setattr(cls, col_name, col_index)

        cls.column_types = column_types
        cls.column_ids = tuple(column_indices)


class Manager (object):

    """GUI Manager base class."""

    @classmethod
    def iter_item_classes(cls):

        msg = "%s class does not support manager item class access"
        raise NotImplementedError(msg % (cls.__name__,))

    @classmethod
    def find_item_class(self, **kw):

        return self.__find_by_attrs(self.iter_item_classes(), kw)

    def iter_items(self):

        msg = "%s object does not support manager item access"
        raise NotImplementedError(msg % (type(self).__name__,))

    def find_item(self, **kw):

        return self.__find_by_attrs(self.iter_items(), kw)

    @staticmethod
    def __find_by_attrs(i, kw):

        from operator import attrgetter

        if len(kw) != 1:
            raise ValueError("need exactly one keyword argument")

        attr, value = list(kw.items())[0]
        getter = attrgetter(attr)

        for item in i:
            if getter(item) == value:
                return item
        else:
            raise KeyError("no item such that item.%s == %r" % (attr, value,))


class StateString (object):

    """Descriptor for binding to StateSection classes."""

    def __init__(self, option, default=None):

        self.option = option
        self.default = default

    def __get__(self, section, section_class=None):

        import configparser

        if section is None:
            return self

        try:
            return self.get(section)
        except (configparser.NoSectionError,
                configparser.NoOptionError,):
            return self.get_default(section)

    def __set__(self, section, value):

        import configparser

        self.set(section, value)

    def get(self, section):

        return section.get(self)

    def get_default(self, section):

        return self.default

    def set(self, section, value):

        if value is None:
            value = ""

        section.set(self, str(value))


class StateBool (StateString):

    """Descriptor for binding to StateSection classes."""

    def get(self, section):

        return section.state._parser.getboolean(section._name, self.option)


class StateInt (StateString):

    """Descriptor for binding to StateSection classes."""

    def get(self, section):

        return section.state._parser.getint(section._name, self.option)


class StateInt4 (StateString):

    """Descriptor for binding to StateSection classes.  This implements storing
    a tuple of 4 integers."""

    def get(self, section):

        value = StateString.get(self, section)

        try:
            l = value.split(",")
            if len(l) != 4:
                return None
            else:
                return tuple((int(v) for v in l))
        except (AttributeError, TypeError, ValueError,):
            return None

    def set(self, section, value):

        if value is None:
            svalue = ""
        elif len(value) != 4:
            raise ValueError("value needs to be a 4-sequence, or None")
        else:
            svalue = ", ".join((str(v) for v in value))

        return StateString.set(self, section, svalue)


class StateItem (StateString):

    """Descriptor for binding to StateSection classes.  This implements storing
    a class controlled by a Manager class."""

    def __init__(self, option, manager_class, default=None):

        StateString.__init__(self, option, default=default)

        self.manager = manager_class

    def get(self, section):

        value = SectionString.get(self, section)

        if not value:
            return None

        return self.parse_item(value)

    def set(self, section, value):

        if value is None:
            svalue = ""
        else:
            svalue = value.name

        StateString.set(self, section, svalue)

    def parse_item(self, value):

        name = value.strip()

        try:
            return self.manager.find_item_class(name=name)
        except KeyError:
            return None


class StateItemList (StateItem):

    """Descriptor for binding to StateSection classes.  This implements storing
    an ordered set of Manager items."""

    def get(self, section):

        value = StateString.get(self, section)

        if not value:
            return []

        classes = []
        for name in value.split(","):
            item_class = self.parse_item(name)
            if item_class is None:
                continue
            if not item_class in classes:
                classes.append(item_class)

        return classes

    def get_default(self, section):

        default = StateItem.get_default(self, section)
        if default is None:
            return []
        else:
            return default

    def set(self, section, value):

        if value is None:
            svalue = ""
        else:
            svalue = ", ".join((v.name for v in value))

        StateString.set(self, section, svalue)


class StateSection (object):

    _name = None

    def __init__(self, state):

        self.state = state

        if self._name is None:
            raise NotImplementedError(
                "subclasses must override the _name attribute")

    def get(self, state_string):

        return self.state._parser.get(self._name, state_string.option)

    def set(self, state_string, value):

        import configparser

        parser = self.state._parser

        try:
            parser.set(self._name, state_string.option, value)
        except configparser.NoSectionError:
            parser.add_section(self._name)
            parser.set(self._name, state_string.option, value)


class State (object):

    def __init__(self, filename, old_filenames=()):

        import configparser

        self.sections = {}

        self._filename = filename
        self._parser = configparser.RawConfigParser()
        success = self._parser.read([filename])
        if not success:
            for old_filename in old_filenames:
                success = self._parser.read([old_filename])
                if success:
                    break

    def add_section_class(self, section_class):

        self.sections[section_class._name] = section_class(self)

    def save(self):

        with utils.SaveWriteFile(self._filename, "wt") as fp:
            self._parser.write(fp)


class WindowState (object):

    def __init__(self):

        self.logger = logging.getLogger("ui.window-state")

        self.is_maximized = False

    def attach(self, window, state):

        self.window = window
        self.state = state

        self.window.connect("window-state-event",
                            self.handle_window_state_event)

        geometry = self.state.geometry
        if geometry:
            self.window.move(*geometry[:2])
            self.window.set_default_size(*geometry[2:])

        if self.state.maximized:
            self.logger.debug("initially maximized")
            self.window.maximize()

    def detach(self):

        window = self.window

        self.state.maximized = self.is_maximized
        if not self.is_maximized:
            position = tuple(window.get_position())
            size = tuple(window.get_size())
            self.state.geometry = position + size

        self.window.disconnect_by_func(self.handle_window_state_event)
        self.window = None

    def handle_window_state_event(self, window, event):

        if not event.changed_mask & Gdk.WindowState.MAXIMIZED:
            return

        if event.new_window_state & Gdk.WindowState.MAXIMIZED:
            self.logger.debug("maximized")
            self.is_maximized = True
        else:
            self.logger.debug("unmaximized")
            self.is_maximized = False
