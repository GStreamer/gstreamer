
__all__ = ["_", "FeatureBase", "PluginBase"]

import os.path
from gettext import gettext as _

def load (paths = ()):

    for path in paths:
        for plugin_module in _load_plugins (path):
            yield plugin_module.Plugin

def _load_plugins (path):

    import imp, glob

    files = glob.glob (os.path.join (path, "*.py"))

    for filename in files:

        name = os.path.basename (os.path.splitext (filename)[0])
        if name == "__init__":
            continue
        fp, pathname, description = imp.find_module (name, [path])
        module = imp.load_module (name, fp, pathname, description)
        yield module

class FeatureBase (object):

    state_section_name = None

    def register_lazy_sentinel (self, sentinel):

        pass

class PluginBase (object):

    features = ()

    def __init__ (self):

        pass
