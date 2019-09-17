#!/usr/bin/env python3

import argparse
import os
import json

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(dest="output", help="Output file")
    parser.add_argument(dest="plugins", nargs=argparse.REMAINDER, help="The list of plugins")

    options = parser.parse_args()

    all_paths = set()
    for plugin in options.plugins:
        all_paths.add(os.path.dirname(plugin))

    with open(options.output, "w") as f:
        json.dump(list(all_paths), f, indent=4, sort_keys=True)
