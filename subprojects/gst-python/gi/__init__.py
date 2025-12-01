import os
import sys
import typing
from importlib.machinery import PathFinder
from pathlib import Path

# Remove this dummy module the python path and
# try to import the actual gi module
sys.path.remove(str(Path(__file__).parents[1]))
del sys.modules["gi"]
import gi


if typing.TYPE_CHECKING:
    # Stubs for type checking our overrides
    def require_version(namespace: str, version: str) -> None:
        pass


class GstOverrideImport:

    def find_spec(self, fullname, path, target=None):
        if not fullname.startswith("gi.overrides"):
            return None
        finder = PathFinder()
        # From find_spec the docs:
        # If name is for a submodule (contains a dot), the parent module is automatically imported.
        spec = finder.find_spec(
            fullname,
            os.environ.get('_GI_OVERRIDES_PATH', '').split(os.pathsep)
        )
        return spec


sys.meta_path.insert(0, GstOverrideImport())
