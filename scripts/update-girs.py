#!/usr/bin/python3
import os
import sys
from lxml import etree

from pathlib import Path as P
import argparse

PARSER = argparse.ArgumentParser()
PARSER.add_argument('builddir')
PARSER.add_argument('girs', nargs="+")


def make_rel(elem, gir_relpath):
    filepath = P(elem.attrib["filename"])
    filedir = filepath.parent
    girdir = gir_relpath.parent

    while filedir.name != girdir.name:
        filedir = filedir.parent

    while filedir.name == girdir.name:
        filedir = filedir.parent
        girdir = girdir.parent
    elem.attrib["filename"] = str('..' / girdir / filepath)


def normalize_shared_library(namespace_elem):
    """Replace .dylib with .so.0 in shared-library attribute"""
    if "shared-library" in namespace_elem.attrib:
        shared_lib = namespace_elem.attrib["shared-library"]
        if shared_lib.endswith(".0.dylib"):
            normalized_lib = shared_lib.replace(".0.dylib", ".so.0")
            namespace_elem.attrib["shared-library"] = normalized_lib


if __name__ == "__main__":
    opts = PARSER.parse_args()
    girdir = P(__file__).parent.parent / 'girs'

    for girfile in opts.girs:
        gir_relpath = P(os.path.relpath(girfile, opts.builddir))
        et = etree.parse(girfile)
        # Remove line numbers from the girs as those would change all the time.
        for n in et.iter("{http://www.gtk.org/introspection/core/1.0}source-position"):
            del n.attrib["line"]
            make_rel(n, gir_relpath)
        for n in et.iter("{http://www.gtk.org/introspection/core/1.0}doc"):
            del n.attrib["line"]
            make_rel(n, gir_relpath)
        # Normalize shared library names
        for namespace in et.iter("{http://www.gtk.org/introspection/core/1.0}namespace"):
            normalize_shared_library(namespace)
        et.write(str(girdir / os.path.basename(girfile)), pretty_print=True)
