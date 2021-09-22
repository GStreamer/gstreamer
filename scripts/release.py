#!/usr/bin/env python3

import os
import shutil
import sys
import tarfile

from collections.abc import MutableSet
import json


# Recipe from http://code.activestate.com/recipes/576694/


class OrderedSet(MutableSet):
    def __init__(self, iterable=None):
        self.end = end = []
        end += [None, end, end]         # sentinel node for doubly linked list
        self.map = {}                   # key --> [key, prev, next]
        if iterable is not None:
            self |= iterable

    def __len__(self):
        return len(self.map)

    def __contains__(self, key):
        return key in self.map

    # pylint: disable=arguments-differ
    def add(self, key):
        if key not in self.map:
            end = self.end
            curr = end[1]
            curr[2] = end[1] = self.map[key] = [key, curr, end]

    def __getstate__(self):
        if not self:
            # The state can't be an empty list.
            # We need to return a truthy value, or else
            # __setstate__ won't be run.
            #
            # This could have been done more gracefully by always putting
            # the state in a tuple, but this way is backwards- and forwards-
            # compatible with previous versions of OrderedSet.
            return (None,)
        return list(self)

    def __setstate__(self, state):
        if state == (None,):
            self.__init__([])
        else:
            self.__init__(state)

    def discard(self, key):
        if key in self.map:
            key, prev, nxt = self.map.pop(key)
            prev[2] = nxt
            nxt[1] = prev

    def __iter__(self):
        end = self.end
        curr = end[2]
        while curr is not end:
            yield curr[0]
            curr = curr[2]

    def __reversed__(self):
        end = self.end
        curr = end[1]
        while curr is not end:
            yield curr[0]
            curr = curr[1]

    def pop(self, last=True):
        if not self:
            raise KeyError('set is empty')
        key = self.end[1][0] if last else self.end[2][0]
        self.discard(key)
        return key

    def __repr__(self):
        if not self:
            return '%s()' % (self.__class__.__name__,)
        return '%s(%r)' % (self.__class__.__name__, list(self))

    def __eq__(self, other):
        if isinstance(other, OrderedSet):
            return len(self) == len(other) and list(self) == list(other)
        return set(self) == set(other)


HERE = os.path.realpath(os.path.dirname(__file__))


if __name__ == "__main__":
    files = sys.argv[1]
    version = sys.argv[2]
    release_name = 'gstreamer-docs-' + sys.argv[2]
    builddir = sys.argv[3]
    readme = os.path.join(builddir, "README.md")
    outname = release_name + '.tar.xz'

    version_v = version.split('.')
    version_major_minor = version_v[0] + '.' + version_v[1]
    symbols_index_dir = os.path.join(HERE, '..', 'symbols')
    symbols_version = '-1'
    symbols_version_file = os.path.join(symbols_index_dir, 'symbols_version.txt')
    try:
        with open(symbols_version_file) as sv:
            symbols_version = sv.read()
    except FileNotFoundError:
        pass

    if symbols_version != version_major_minor:
        print("Updating symbols to new major version %s" % version_major_minor, file=sys.stderr)

        symbol_index_file = os.path.join(symbols_index_dir, 'symbol_index.json')
        with open(symbol_index_file, 'r', encoding='utf-8') as _:
            symbols = OrderedSet(json.load(_))

        with open(os.path.join(builddir, "hotdoc-private-GStreamer", "symbol_index.json"), 'r', encoding='utf-8') as _:
            new_symbols = OrderedSet(sorted(json.load(_)))

        with open(symbol_index_file, 'w', encoding='utf-8') as _:
            json.dump(sorted(list(symbols | new_symbols)), _, indent=2)

        with open(symbols_version_file, 'w') as sv:
            sv.write(version_major_minor)

        print("NOTE: YOU SHOULD COMMIT THE FOLLOWING FILES BEFORE PUBLISHING THE RELEASE:", file=sys.stderr)
        print(" - " + symbol_index_file, file=sys.stderr)
        print(" - " + symbols_version_file, file=sys.stderr)

        sys.exit(1)

    print("Generating %s" % os.path.realpath(os.path.join(os.path.curdir, outname)), file=sys.stderr)

    # Filter out duplicate js search assets for devhelp dir
    def exclude_filter(tarinfo):
        if '/devhelp/books/GStreamer/' in tarinfo.name:
            if '/assets/fonts' in tarinfo.name:
                return None
            if '/assets/js/search' in tarinfo.name:
                return None
            if '/dumped.trie' in tarinfo.name:
                return None

        return tarinfo

    tar = tarfile.open(outname, 'w:xz')
    tar.add(files, release_name, filter=exclude_filter)
    for license in ['LICENSE.BSD', 'LICENSE.CC-BY-SA-4.0', 'LICENSE.LGPL-2.1', 'LICENSE.MIT', 'LICENSE.OPL']:
        tar.add(os.path.join(HERE, '..', license), os.path.join(release_name, license))
    os.chdir(os.path.dirname(readme))
    tar.add(os.path.basename(readme), os.path.join(release_name, os.path.basename(readme)))
    tar.close()
