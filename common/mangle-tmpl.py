# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

"""
use the output from gst-xmlinspect.py to mangle tmpl/*.sgml and
insert/overwrite Short Description and Long Description
"""

# FIXME: right now it uses pygst and scans on its own;
# we really should use inspect/*.xml instead since the result of
# gst-xmlinspect.py is commited by the docs maintainer, who can be
# expected to have pygst, but this step should be done for every docs build,
# so no pygst allowed

# read in inspect/*.xml
# for every tmpl/element-(name).xml: mangle with details from element

import glob
import re
import sys
import os

class Tmpl:
    def __init__(self, filename):
        self.filename = filename
        self._sectionids = []
        self._sections = {}

    def read(self):
        """
        Read and parse the sections from the given file.
        """
        lines = open(self.filename).readlines()
        matcher = re.compile("<!-- ##### SECTION (\S+) ##### -->\n")
        id = None

        for line in lines:
            match = matcher.search(line)
            if match:
                id = match.expand("\\1")
                self._sectionids.append(id)
                self._sections[id] = []
            else:
                if not id:
                    sys.stderr.write(
                        "WARNING: line before a SECTION header: %s" % line)
                else:
                    self._sections[id].append(line)

    def get_section(self, id):
        """
        Get the content from the given section.
        """
        return self._sections[id]

    def set_section(self, id, content):
        """
        Replace the given section id with the given content.
        """
        self._sections[id] = content

    def output(self):
        """
        Return the output of the current template in the tmpl/*.sgml format.
        """
        lines = []
        for id in self._sectionids:
            lines.append("<!-- ##### SECTION %s ##### -->\n" % id)
            for line in self._sections[id]:
                lines.append(line)

        return "".join(lines)

    def write(self, backup=False):
        """
        Write out the template file again, backing up the previous one.
        """
        if backup:
            target = self.filename + ".mangle.bak"
            os.rename(self.filename, target)

        handle = open(self.filename, "w")
        handle.write(self.output())
        handle.close()

from xml.dom.ext.reader import Sax2
from xml.dom.NodeFilter import NodeFilter

def get_elements(file):
    elements = {}
    handle = open(file)
    reader = Sax2.Reader()
    doc = reader.fromStream(handle)
    handle.close()

    walker = doc.createTreeWalker(doc.documentElement,
        NodeFilter.SHOW_ELEMENT, None, 0)
    while walker.currentNode and walker.currentNode.tagName != 'elements':
        walker.nextNode()
        
    # we're at elements now
    el = walker.firstChild()
    while walker.currentNode:
        element = walker.firstChild()
        # loop over children of <element>
        name = None
        description = None
        while walker.currentNode:
            if walker.currentNode.tagName == 'name':
                name = walker.currentNode.firstChild.data.encode('UTF-8')
            if walker.currentNode.tagName == 'description':
                description = walker.currentNode.firstChild.data.encode('UTF-8')
            if not walker.nextSibling(): break
        # back up to <element>
        walker.parentNode()
        elements[name] = {'description': description}

        if not walker.nextSibling(): break

    return elements

        
def main():
    if not len(sys.argv) == 3:
        sys.stderr.write('Please specify the inspect/ dir and the tmpl/ dir')
        sys.exit(1)

    inspectdir = sys.argv[1]
    tmpldir = sys.argv[2]

    # parse all .xml files; build map of element name -> short desc
    #for file in glob.glob("inspect/plugin-*.xml"):
    elements = {}
    for file in glob.glob("%s/plugin-*.xml" % inspectdir):
        elements.update(get_elements(file))

    for file in glob.glob("%s/element-*.sgml" % tmpldir):
        base = os.path.basename(file)
        element = base[len("element-"):-len(".sgml")]
        tmpl = Tmpl(file)
        tmpl.read()
        if element in elements.keys():
            description = elements[element]['description']
            tmpl.set_section("Short_Description", "%s\n\n" % description)

        # put in an include if not yet there
        line = '<include xmlns="http://www.w3.org/2003/XInclude" href="' + \
            'element-' + element + '-details.xml" />\n'
        section = tmpl.get_section("Long_Description")
        if not section[0]  == line:
            section.insert(0, line)
        tmpl.set_section("Long_Description", section)
        tmpl.write()

main()
