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

"""GStreamer development utilities common GUI module"""

import logging

import pygtk
pygtk.require ("2.0")
del pygtk

import gobject
import gtk

from GstDebugViewer.Common import utils

class Actions (dict):

    def __init__ (self):

        dict.__init__ (self)

        self.groups = ()

    def __getattr__ (self, name):

        try:
            return self[name]
        except KeyError:
            if "_" in name:
                try:
                    return self[name.replace ("_", "-")]
                except KeyError:
                    pass

        raise AttributeError ("no action with name %r" % (name,))

    def add_group (self, group):

        self.groups += (group,)
        for action in group.list_actions ():
            self[action.props.name] = action

class Widgets (dict):

    def __init__ (self, glade_tree):

        widgets = glade_tree.get_widget_prefix ("")
        dict.__init__ (self, ((w.name, w,) for w in widgets))

    def __getattr__ (self, name):

        try:
            return self[name]
        except KeyError:
            if "_" in name:
                try:
                    return self[name.replace ("_", "-")]
                except KeyError:
                    pass

        raise AttributeError ("no widget with name %r" % (name,))

class WidgetFactory (object):

    def __init__ (self, glade_filename):

        self.filename = glade_filename

    def make (self, widget_name, autoconnect = None):

        glade_tree = gtk.glade.XML (self.filename, widget_name)

        if autoconnect is not None:
            glade_tree.signal_autoconnect (autoconnect)

        return Widgets (glade_tree)

    def make_one (self, widget_name):

        glade_tree = gtk.glade.XML (self.filename, widget_name)

        return glade_tree.get_widget (widget_name)

class UIFactory (object):

    def __init__ (self, ui_filename, actions = None):

        self.filename = ui_filename
        if actions:
            self.action_groups = actions.groups
        else:
            self.action_groups = ()

    def make (self, extra_actions = None):

        ui_manager = gtk.UIManager ()
        for action_group in self.action_groups:
            ui_manager.insert_action_group (action_group, 0)
        if extra_actions:
            for action_group in extra_actions.groups:
                ui_manager.insert_action_group (action_group, 0)
        ui_manager.add_ui_from_file (self.filename)
        ui_manager.ensure_update ()

        return ui_manager

class MetaModel (gobject.GObjectMeta):

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

    Example: A gtk.ListStore derived model can use
    
        columns = ("COL_NAME", str, "COL_VALUE", str)
        
    and use this in __init__:
    
        gtk.ListStore.__init__ (self, *self.column_types)

    Then insert data like this:

        self.set (self.append (),
                  self.COL_NAME, "spam",
                  self.COL_VALUE, "ham")
    """
    
    def __init__ (cls, name, bases, dict):
        
        super (MetaModel, cls).__init__ (name, bases, dict)
        
        spec = tuple (cls.columns)
        
        column_names = spec[::2]
        column_types = spec[1::2]
        column_indices = range (len (column_names))
        
        for col_index, col_name, in zip (column_indices, column_names):
            setattr (cls, col_name, col_index)
        
        cls.column_types = column_types
        cls.column_ids = tuple (column_indices)

class Manager (object):

    """GUI Manager base class."""

    @classmethod
    def iter_item_classes (cls):

        msg = "%s class does not support manager item class access"
        raise NotImplementedError (msg % (cls.__name__,))

    @classmethod
    def find_item_class (self, **kw):

        return self.__find_by_attrs (self.iter_item_classes (), kw)

    def iter_items (self):

        msg = "%s object does not support manager item access"
        raise NotImplementedError (msg % (type (self).__name__,))

    def find_item (self, **kw):

        return self.__find_by_attrs (self.iter_items (), kw)

    @staticmethod
    def __find_by_attrs (i, kw):

        from operator import attrgetter

        if len (kw) != 1:
            raise ValueError ("need exactly one keyword argument")

        attr, value = kw.items ()[0]
        getter = attrgetter (attr)

        for item in i:
            if getter (item) == value:
                return item
        else:
            raise KeyError ("no item such that item.%s == %r" % (attr, value,))

class StateString (object):

    """Descriptor for binding to AppState classes."""

    def __init__ (self, option, section = None):

        self.option = option
        self.section = section

    def get_section (self, state):

        if self.section is None:
            return state._default_section
        else:
            return self.section

    def get_getter (self, state):

        return state._parser.get

    def get_default (self, state):

        return None

    def __get__ (self, state, state_class = None):

        import ConfigParser

        if state is None:
            return self

        getter = self.get_getter (state)
        section = self.get_section (state)

        try:
            return getter (section, self.option)
        except (ConfigParser.NoSectionError,
                ConfigParser.NoOptionError,):
            return self.get_default (state)

    def __set__ (self, state, value):

        import ConfigParser

        if value is None:
            value = ""

        section = self.get_section (state)
        option = self.option
        option_value = str (value)

        try:
            state._parser.set (section, option, option_value)
        except ConfigParser.NoSectionError:
            state._parser.add_section (section)
            state._parser.set (section, option, option_value)

class StateBool (StateString):

    """Descriptor for binding to AppState classes."""

    def get_getter (self, state):

        return state._parser.getboolean

class StateInt (StateString):

    """Descriptor for binding to AppState classes."""

    def get_getter (self, state):

        return state._parser.getint

class StateInt4 (StateString):

    """Descriptor for binding to AppState classes.  This implements storing a
    tuple of 4 integers."""

    def __get__ (self, state, state_class = None):

        if state is None:
            return self

        value = StateString.__get__ (self, state)

        try:
            l = value.split (",")
            if len (l) != 4:
                return None
            else:
                return tuple ((int (v) for v in l))
        except (AttributeError, TypeError, ValueError,):
            return None

    def __set__ (self, state, value):

        if value is None:
            svalue = ""
        elif len (value) != 4:
            raise ValueError ("value needs to be a 4-sequence, or None")
        else:
            svalue = ", ".join ((str (v) for v in value))

        return StateString.__set__ (self, state, svalue)

class StateItem (StateString):

    """Descriptor for binding to AppState classes.  This implements storing a
    class controlled by a Manager class."""

    def __init__ (self, option, manager_class, section = None):

        StateString.__init__ (self, option, section = section)

        self.manager = manager_class

    def __get__ (self, state, state_class = None):

        if state is None:
            return self

        value = StateString.__get__ (self, state)

        if not value:
            return None

        return self.parse_item (value)
        
    def __set__ (self, state, value):

        if value is None:
            svalue = ""
        else:
            svalue = value.name

        StateString.__set__ (self, state, svalue)

    def parse_item (self, value):

        name = value.strip ()

        try:
            return self.manager.find_item_class (name = name)
        except KeyError:
            return None        

class StateItemList (StateItem):

    """Descriptor for binding to AppState classes.  This implements storing an
    ordered set of Manager items."""

    def __get__ (self, state, state_class = None):

        if state is None:
            return self

        value = StateString.__get__ (self, state)

        if not value:
            return []

        classes = []
        for name in value.split (","):
            item_class = self.parse_item (name)
            if item_class is None:
                continue
            if not item_class in classes:
                classes.append (item_class)

        return classes

    def __set__ (self, state, value):

        if value is None:
            svalue = ""
        else:
            svalue = ", ".join ((v.name for v in value))
        
        StateString.__set__ (self, state, svalue)

class AppState (object):

    _default_section = "state"

    def __init__ (self, filename, old_filenames = ()):

        import ConfigParser

        self._filename = filename
        self._parser = ConfigParser.RawConfigParser ()
        success = self._parser.read ([filename])
        if not success:
            for old_filename in old_filenames:
                success = self._parser.read ([old_filename])
                if success:
                    break

    def save (self):

        # TODO Py2.5: Use 'with' statement.
        fp = utils.SaveWriteFile (self._filename, "wt")
        try:
            self._parser.write (fp)
        except:
            fp.discard ()
        else:
            fp.close ()

class WindowState (object):

    def __init__ (self):

        self.logger = logging.getLogger ("ui.window-state")

        self.is_maximized = False

    def attach (self, window, state):

        self.window = window
        self.state = state
        
        self.window.connect ("window-state-event",
                             self.handle_window_state_event)

        geometry = self.state.geometry
        if geometry:
            self.window.move (*geometry[:2])
            self.window.set_default_size (*geometry[2:])

        if self.state.maximized:
            self.logger.debug ("initially maximized")
            self.window.maximize ()

    def detach (self):

        window = self.window

        self.state.maximized = self.is_maximized
        if not self.is_maximized:
            position = tuple (window.get_position ())
            size = tuple (window.get_size ())
            self.state.geometry = position + size

        self.window.disconnect_by_func (self.handle_window_state_event)
        self.window = None

    def handle_window_state_event (self, window, event):

        if not event.changed_mask & gtk.gdk.WINDOW_STATE_MAXIMIZED:
            return
        
        if event.new_window_state & gtk.gdk.WINDOW_STATE_MAXIMIZED:
            self.logger.debug ("maximized")
            self.is_maximized = True
        else:
            self.logger.debug ("unmaximized")
            self.is_maximized = False
