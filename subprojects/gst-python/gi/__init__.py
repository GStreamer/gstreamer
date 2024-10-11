import gi
import os
import sys
import importlib.util
from importlib.machinery import PathFinder
from pathlib import Path

# Remove this dummy module the python path and
# try to import the actual gi module
sys.path.remove(str(Path(__file__).parents[1]))
del sys.modules["gi"]
import gi


class GstOverrideImport:
    def find_spec(self, fullname, path, target=None):
        if not fullname.startswith("gi.overrides"):
            return None
        finder = importlib.machinery.PathFinder()
        # From find_spec the docs:
        # If name is for a submodule (contains a dot), the parent module is automatically imported.
        spec = finder.find_spec(
            fullname,
            os.environ.get('_GI_OVERRIDES_PATH', '').split(os.pathsep)
        )
        return spec


sys.meta_path.insert(0, GstOverrideImport())
