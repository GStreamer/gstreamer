import os
import sys
import importlib


class GstOverrideImport:
    def find_spec(self, fullname, path, target=None):
        if not (fullname.startswith("gi.overrides.Gst") or fullname.startswith("gi.overrides._gi_gst")):
            return None
        finder = importlib.machinery.PathFinder()
        # From find_spec the docs:
        # If name is for a submodule (contains a dot), the parent module is automatically imported.
        spec = finder.find_spec(
            fullname,
            [
                os.environ.get('GST_OVERRIDE_SRC_PATH'),
                os.environ.get('GST_OVERRIDE_BUILD_PATH'),
            ]
        )
        return spec


sys.meta_path.insert(0, GstOverrideImport())
