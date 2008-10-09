# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

"""
Convert a C program to valid XML to be included in docbook
"""

import sys
import os
from xml.sax import saxutils

def main():
    if len(sys.argv) == 1:
        sys.stderr.write("Please specify a source file to convert")
        sys.exit(1)
    source = sys.argv[1]

    if not os.path.exists(source):
        sys.stderr.write("%s does not exist.\n" % source)
        sys.exit(1)

    content = open(source, "r").read()

    # print header
    print '<?xml version="1.0"?>'
    print '<!DOCTYPE refentry PUBLIC "-//OASIS//DTD DocBook XML V4.1.2//EN" "http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd">'
    print
    print '<programlisting>'

    # print content
    print saxutils.escape(content).encode('UTF-8')
    print '</programlisting>'
        
main()
