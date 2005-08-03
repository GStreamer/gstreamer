#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# GStreamer python bindings
# Copyright (C) 2002 David I. Lehn <dlehn@users.sourceforge.net>
#               2004 Johan Dahlin  <johan@gnome.org>

# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
# 
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
# 
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

from common import gst, unittest

class PadTest(unittest.TestCase):
        
    def testQuery(self):
        xml = gst.XML()
        xml.parse_memory("""<?xml version="1.0"?>
<gstreamer xmlns:gst="http://gstreamer.net/gst-core/1.0/">
  <gst:element>
    <gst:name>test-pipeline</gst:name>
    <gst:type>pipeline</gst:type>
    <gst:param>
      <gst:name>name</gst:name>
      <gst:value>test-pipeline</gst:value>
    </gst:param>
  </gst:element>
</gstreamer>""")
        elements = xml.get_topelements()
        assert len(elements) == 1
        element = elements[0]
        assert isinstance(element, gst.Pipeline)
        assert element.get_name() == 'test-pipeline'
        
if __name__ == "__main__":
    unittest.main()
        
