#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

import sys

import pygtk
pygtk.require('2.0')

from gobject.option import OptionParser, OptionGroup
import pygst
pygst.require('0.10')

import gstoption

def main(args):
    parser = OptionParser()

    group = OptionGroup('flumotion', 'Flumotion options',
                        option_list=[])
    group.add_option('-v', '--verbose',
                      action="store_true", dest="verbose",
                      help="be verbose")
    group.add_option('', '--version',
                      action="store_true", dest="version",
                      default=False,
                      help="show version information")
    parser.add_option_group(group)

    parser.add_option_group(gstoption.get_group())

    options, args = parser.parse_args(args)

    if options.verbose:
        print 'Verbose mode'

    import gst

    if options.version:
        print sys.version, gst.version

if __name__ == '__main__':
    sys.exit(main(sys.argv))
