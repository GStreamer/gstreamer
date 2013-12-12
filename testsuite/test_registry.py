# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2002 David I. Lehn
# Copyright (C) 2004 Johan Dahlin
# Copyright (C) 2005 Edward Hervey
#
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA

import sys
import gc
from common import gst, unittest, TestCase

class RegistryTest(TestCase):
    def setUp(self):
        self.registry = gst.registry_get_default()
        self.plugins = self.registry.get_plugin_list()
        TestCase.setUp(self)

    def testGetDefault(self):
        assert(self.registry)

    def testPluginList(self):
        names = map(lambda p: p.get_name(), self.plugins)
        self.failUnless('staticelements' in names)

    def testGetPathList(self):
        # FIXME: this returns an empty list; probably due to core;
        # examine problem
        
        paths = self.registry.get_path_list()

class RegistryFeatureTest(TestCase):
    def setUp(self):
        self.registry = gst.registry_get_default()
        self.plugins = self.registry.get_plugin_list()
        self.efeatures = self.registry.get_feature_list(gst.TYPE_ELEMENT_FACTORY)
        self.tfeatures = self.registry.get_feature_list(gst.TYPE_TYPE_FIND_FACTORY)
        self.ifeatures = self.registry.get_feature_list(gst.TYPE_INDEX_FACTORY)
        TestCase.setUp(self)

    def testFeatureList(self):
        self.assertRaises(TypeError, self.registry.get_feature_list, "kaka")
        
        elements = map(lambda f: f.get_name(), self.efeatures)
        self.failUnless('fakesink' in elements)

        typefinds = map(lambda f: f.get_name(), self.tfeatures)

        indexers = map(lambda f: f.get_name(), self.ifeatures)
        self.failUnless('memindex' in indexers)
        
        
if __name__ == "__main__":
    unittest.main()
