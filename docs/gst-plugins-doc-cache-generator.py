#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright © 2018 Thibault Saunier <tsaunier@igalia.com>
#
# This library is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 2.1 of the License, or (at your option)
# any later version.
#
# This library is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this library.  If not, see <http://www.gnu.org/licenses/>.

import argparse
import json
import os
import sys
import subprocess

from collections import OrderedDict
try:
    from collections.abc import Mapping
except ImportError:  # python <3.3
    from collections import Mapping


# Marks values in the json file as "unstable" so that they are
# not updated automatically, this aims at making the cache file
# stable and handle corner cases were we can't automatically
# make it happen. For properties, the best way is to use th
# GST_PARAM_DOC_SHOW_DEFAULT flag.
UNSTABLE_VALUE = "unstable-values"


def dict_recursive_update(d, u):
    unstable_values = d.get(UNSTABLE_VALUE, [])
    if not isinstance(unstable_values, list):
        unstable_values = [unstable_values]
    for k, v in u.items():
        if isinstance(v, Mapping):
            r = dict_recursive_update(d.get(k, {}), v)
            d[k] = r
        elif k not in unstable_values:
            d[k] = u[k]
    return d


def test_unstable_values():
    current_cache = { "v1": "yes", "unstable-values": "v1"}
    new_cache = { "v1": "no" }

    assert(dict_recursive_update(current_cache, new_cache) == current_cache)

    new_cache = { "v1": "no", "unstable-values": "v2" }
    assert(dict_recursive_update(current_cache, new_cache) == new_cache)

    current_cache = { "v1": "yes", "v2": "yay", "unstable-values": "v1",}
    new_cache = { "v1": "no" }
    assert(dict_recursive_update(current_cache, new_cache) == current_cache)

    current_cache = { "v1": "yes", "v2": "yay", "unstable-values": "v2"}
    new_cache = { "v1": "no", "v2": "unstable" }
    assert(dict_recursive_update(current_cache, new_cache) == { "v1": "no", "v2": "yay", "unstable-values": "v2" })

if __name__ == "__main__":
    cache_filename = sys.argv[1]
    output_filename = sys.argv[2]

    subenv = os.environ.copy()
    cache = {}
    try:
        with open(cache_filename) as f:
            cache = json.load(f)
    except FileNotFoundError:
        pass

    cmd = [os.path.join(os.path.dirname(os.path.realpath(__file__)), 'gst-hotdoc-plugins-scanner')]
    gst_plugins_paths = []
    for plugin_path in sys.argv[3:]:
        cmd.append(plugin_path)
        gst_plugins_paths.append(os.path.dirname(plugin_path))

    if subenv.get('GST_REGISTRY_UPDATE') != 'no' and len(cmd) >= 2:
        data = subprocess.check_output(cmd, env=subenv)
        try:
            plugins = json.loads(data.decode(), object_pairs_hook=OrderedDict)
        except json.decoder.JSONDecodeError:
            print("Could not decode:\n%s" % data.decode(), file=sys.stderr)
            raise

    new_cache = dict_recursive_update(cache, plugins)

    with open(output_filename, 'w') as f:
        json.dump(cache, f, indent=4, sort_keys=True)

    if new_cache != cache:
        with open(cache_filename, 'w') as f:
            json.dump(cache, f, indent=4, sort_keys=True)