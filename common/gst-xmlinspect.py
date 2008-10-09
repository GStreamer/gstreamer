# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

"""
examine all plugins and elements and output xml documentation for them
used as part of the plugin documentation build
"""

import sys
import os
import pygst
pygst.require('0.10')
import gst

INDENT_SIZE = 2

# all templates

PAD_TEMPLATE = """<caps>
  <name>%(name)s</name>
  <direction>%(direction)s</direction>
  <presence>%(presence)s</presence>
  <details>%(details)s</details>
</caps>"""

ELEMENT_TEMPLATE = """<element>
  <name>%(name)s</name>
  <longname>%(longname)s</longname>
  <class>%(class)s</class>
  <description>%(description)s</description>
  <author>%(author)s</author>
  <pads>
%(pads)s
  </pads>
</element>"""

PLUGIN_TEMPLATE = """<plugin>
  <name>%(name)s</name>
  <description>%(description)s</description>
  <filename>%(filename)s</filename>
  <basename>%(basename)s</basename>
  <version>%(version)s</version>
  <license>%(license)s</license>
  <source>%(source)s</source>
  <package>%(package)s</package>
  <origin>%(origin)s</origin>
  <elements>
%(elements)s
  </elements>
</plugin>"""

def xmlencode(line):
    """
    Replace &, <, and >
    """
    line = "&amp;".join(line.split("&"))
    line = "&lt;".join(line.split("<"))
    line = "&gt;".join(line.split(">"))
    return line

def get_offset(indent):
    return " " * INDENT_SIZE * indent

def output_pad_template(pt, indent=0):
    print  "PAD TEMPLATE", pt.name_template
    paddir = ("unknown","source","sink")
    padpres = ("always","sometimes","request")
    
    d = {
      'name':        xmlencode(pt.name_template),
      'direction':   xmlencode(paddir[pt.direction]),
      'presence':    xmlencode(padpres[pt.presence]),
      'details':     xmlencode(pt.static_caps.string),
    }
    block = PAD_TEMPLATE % d

    offset = get_offset(indent)
    return offset + ("\n" + offset).join(block.split("\n"))
    
def output_element_factory(elf, indent=0):
    print  "ELEMENT", elf.get_name()

    padsoutput = []
    padtemplates = elf.get_static_pad_templates()
    for padtemplate in padtemplates:
        padsoutput.append(output_pad_template(padtemplate, indent))

    d = {
        'name':        xmlencode(elf.get_name()),
        'longname':    xmlencode(elf.get_longname()),
        'class':       xmlencode(elf.get_klass()),
        'description': xmlencode(elf.get_description()),
        'author':      xmlencode(elf.get_author()),
        'pads': "\n".join(padsoutput),
    }
    block = ELEMENT_TEMPLATE % d

    offset = get_offset(indent)
    return offset + ("\n" + offset).join(block.split("\n"))

def output_plugin(plugin, indent=0):
    print "PLUGIN", plugin.get_name()
    version = plugin.get_version()
    
    elements = {}
    gst.debug('getting features for plugin %s' % plugin.get_name())
    registry = gst.registry_get_default()
    features = registry.get_feature_list_by_plugin(plugin.get_name())
    gst.debug('plugin %s has %d features' % (plugin.get_name(), len(features)))
    for feature in features:
        if isinstance(feature, gst.ElementFactory):
            elements[feature.get_name()] = feature
    #gst.debug("got features")
        
    elementsoutput = []
    keys = elements.keys()
    keys.sort()
    for name in keys:
        feature = elements[name]
        elementsoutput.append(output_element_factory(feature, indent + 2))

    filename = plugin.get_filename()
    basename = filename
    if basename:
        basename = os.path.basename(basename)
    d = {
        'name':        xmlencode(plugin.get_name()),
        'description': xmlencode(plugin.get_description()),
        'filename':    filename,
        'basename':    basename,
        'version':     version,
        'license':     xmlencode(plugin.get_license()),
        'source':      xmlencode(plugin.get_source()),
        'package':     xmlencode(plugin.get_package()),
        'origin':      xmlencode(plugin.get_origin()),
        'elements': "\n".join(elementsoutput),
    }
    block = PLUGIN_TEMPLATE % d
    
    offset = get_offset(indent)
    return offset + ("\n" + offset).join(block.split("\n"))

def main():
    if len(sys.argv) == 1:
        sys.stderr.write("Please specify a source module to inspect")
        sys.exit(1)
    source = sys.argv[1]

    if len(sys.argv) > 2:
        os.chdir(sys.argv[2])

    registry = gst.registry_get_default()
    all = registry.get_plugin_list()
    for plugin in all:
        gst.debug("inspecting plugin %s from source %s" % (
            plugin.get_name(), plugin.get_source()))
        # this skips gstcoreelements, with bin and pipeline
        if plugin.get_filename() is None:
            continue
        if plugin.get_source() != source:
            continue

        filename = "plugin-%s.xml" % plugin.get_name()
        handle = open(filename, "w")
        handle.write(output_plugin(plugin))
        handle.close()

main()
