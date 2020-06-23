#!/usr/bin/env python3

import os
import shutil
import sys
import tarfile


HERE = os.path.realpath(os.path.dirname(__file__))


if __name__ == "__main__":
    files = sys.argv[1]
    version = sys.argv[2]
    release_name = 'gstreamer-doc-' + sys.argv[2]
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
        shutil.copyfile(os.path.join(builddir, "hotdoc-private-GStreamer", "symbol_index.json"),
            symbol_index_file)
        with open(symbols_version_file, 'w') as sv:
            sv.write(version_major_minor)
        print("NOTE: YOU SHOULD COMMIT THE FOLLOWING FILES BEFORE PUBLISHING THE RELEASE:", file=sys.stderr)
        print(" - " + symbol_index_file, file=sys.stderr)
        print(" - " + symbols_version_file, file=sys.stderr)

        sys.exit(1)

    print("Generating %s" % os.path.realpath(os.path.join(os.path.curdir, outname)), file=sys.stderr)
    tar = tarfile.open(outname, 'w:xz')
    tar.add(files, release_name)
    os.chdir(os.path.dirname(readme))
    tar.add(os.path.basename(readme), os.path.join(release_name, os.path.basename(readme)))
    tar.close()