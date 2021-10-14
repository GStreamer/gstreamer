import gi
import os
import sys
import imp
from pathlib import Path

# Remove this dummy module the python path and
# try to import the actual gi module
sys.path.remove(str(Path(__file__).parents[1]))
del sys.modules["gi"]
import gi


class GstOverrideImport:
    def find_module(self, fullname, path=None, target=None):
        if fullname.startswith('gi.overrides'):
            fp = None
            try:
                fp, _, _ = imp.find_module(fullname.split(
                    '.')[-1], os.environ.get('_GI_OVERRIDES_PATH', '').split(os.pathsep),)
            except ImportError:
                return None
            finally:
                if fp:
                    fp.close()
            return self
        return None

    def load_module(self, name):
        if name in sys.modules:
            return sys.modules[name]

        fp, pathname, description = imp.find_module(name.split(
            '.')[-1], os.environ.get('_GI_OVERRIDES_PATH', '').split(os.pathsep),)

        try:
            module = imp.load_module(name, fp, pathname, description)
        finally:
            if fp:
                fp.close()
        sys.modules[name] = module
        return module


sys.meta_path.insert(0, GstOverrideImport())
