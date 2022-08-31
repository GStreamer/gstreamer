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
        et.write(str(girdir / os.path.basename(girfile)), pretty_print=True)
