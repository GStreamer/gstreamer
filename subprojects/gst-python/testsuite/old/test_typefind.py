# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2008 Alessandro Decina
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

from common import gst, unittest, TestCase, pygobject_2_13

import sys
import time

class TypeFindTest(TestCase):
    def testTypeFind(self):
        def application_awesome_type_find(typefind, arg1,  arg2):
            self.failUnlessEqual(arg1, 'arg1')
            self.failUnlessEqual(arg2, 'arg2')

            data = typefind.peek(0, 5)
            self.failUnless(data == '', 'peek out of length??')
            
            data = typefind.peek(0, 0)
            self.failUnless(data == '', '0 peek??')

            data = typefind.peek(3, 1)
            self.failUnless(data == 'M')

            data = typefind.peek(0, 4)
            self.failUnless(data == 'AWSM')

            typefind.suggest(gst.TYPE_FIND_MAXIMUM,
                    gst.Caps('application/awesome'))

        res = gst.type_find_register('application/awesome', gst.RANK_PRIMARY,
                application_awesome_type_find, ['.twi'],
                gst.Caps('application/awesome'), 'arg1', 'arg2')
        self.failUnless(res, 'type_find_register failed')

        factory = None
        factories = gst.type_find_factory_get_list()
        for typefind_factory in factories:
            if typefind_factory.get_name() == 'application/awesome':
                factory = typefind_factory
                break
        self.failUnless(factory is not None)

        obj = gst.Pad('src', gst.PAD_SRC)
        buffer = gst.Buffer('AWSM')
        caps, probability =  gst.type_find_helper_for_buffer(obj, buffer)

        self.failUnlessEqual(str(caps), 'application/awesome')
        self.failUnlessEqual(probability, gst.TYPE_FIND_MAXIMUM)
