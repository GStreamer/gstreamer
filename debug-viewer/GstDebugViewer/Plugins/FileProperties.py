
from GstDebugViewer.Plugins import *
import logging
import gtk

class FilePropertiesSentinel (object):

    pass

class FilePropertiesDialog (gtk.Dialog):

    pass

class FilePropertiesFeature (FeatureBase):

    def __init__ (self):

        self.action_group = gtk.ActionGroup ("FilePropertiesActions")
        self.action_group.add_actions ([("show-file-properties", gtk.STOCK_PROPERTIES,
                                         _("_Properties"), "<Ctrl>P")])

    def attach (self, window):

        ui = window.ui_manager
        ui.insert_action_group (self.action_group, 0)

        self.merge_id = ui.new_merge_id ()
        ui.add_ui (self.merge_id, "/menubar/FileMenu/FileMenuAdditions",
                   "FileProperties", "show-file-properties",
                   gtk.UI_MANAGER_MENUITEM, False)

        handler = self.handle_action_activate
        self.action_group.get_action ("show-file-properties").connect ("activate", handler)

    def handle_action_activate (self, action):

        pass

class Plugin (PluginBase):

    features = (FilePropertiesFeature,)
