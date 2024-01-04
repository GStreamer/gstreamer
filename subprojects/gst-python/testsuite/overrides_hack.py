import os
import sys
import imp


class GstOverrideImport:
    def find_module(self, fullname, path=None):
        if fullname in ('gi.overrides.Gst', 'gi.overrides._gi_gst'):
            return self
        return None

    def load_module(self, name):
        if name in sys.modules:
            return sys.modules[name]

        fp, pathname, description = imp.find_module(name.split('.')[-1], [
            os.environ.get('GST_OVERRIDE_SRC_PATH'),
            os.environ.get('GST_OVERRIDE_BUILD_PATH'),
        ])

        try:
            module = imp.load_module(name, fp, pathname, description)
        finally:
            if fp:
                fp.close()
        sys.modules[name] = module
        return module


sys.meta_path.insert(0, GstOverrideImport())
