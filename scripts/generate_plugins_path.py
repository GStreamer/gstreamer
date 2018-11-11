#!/usr/bin/env python3

import argparse
import os
import json

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--builddir", help="The meson build directory")
    parser.add_argument(dest="plugins", help="The list of plugins", nargs="+")

    options = parser.parse_args()

    all_paths = set()
    for plugin in options.plugins:
        all_paths.add(os.path.dirname(plugin))

    with open(os.path.join(options.builddir, 'GstPluginsPath.json'), "w") as f:
        json.dump(list(all_paths), f, indent=4, sort_keys=True)
