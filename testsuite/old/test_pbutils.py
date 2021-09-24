# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2008 Edward Hervey <edward.hervey@collabora.co.uk>
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

from common import gobject, gst, unittest, TestCase

class Descriptions(TestCase):

    def testSourceDescription(self):
        assert hasattr(gst.pbutils, 'get_source_description')
        self.assertEquals(gst.pbutils.get_source_description("file"),
                          "FILE protocol source")

    def testSinkDescription(self):
        assert hasattr(gst.pbutils, 'get_sink_description')
        self.assertEquals(gst.pbutils.get_sink_description("file"),
                          "FILE protocol sink")

    def testDecoderDescription(self):
        assert hasattr(gst.pbutils, 'get_decoder_description')
        self.assertEquals(gst.pbutils.get_decoder_description(gst.caps_from_string("audio/mpeg,mpegversion=1,layer=3")),
                          'MPEG-1 Layer 3 (MP3) decoder')

    def testCodecDescription(self):
        assert hasattr(gst.pbutils, 'get_codec_description')
        self.assertEquals(gst.pbutils.get_codec_description(gst.caps_from_string("audio/mpeg,mpegversion=1,layer=3")),
                          'MPEG-1 Layer 3 (MP3)')

    def testEncoderDescription(self):
        assert hasattr(gst.pbutils, 'get_encoder_description')
        self.assertEquals(gst.pbutils.get_encoder_description(gst.caps_from_string("audio/mpeg,mpegversion=1,layer=3")),
                          'MPEG-1 Layer 3 (MP3) encoder')

    def testElementDescription(self):
        assert hasattr(gst.pbutils, 'get_element_description')
        self.assertEquals(gst.pbutils.get_element_description("something"),
                          "GStreamer element something")

    def testAddCodecDescription(self):
        assert hasattr(gst.pbutils, 'add_codec_description_to_tag_list')

# TODO
# Add tests for the other parts of pbutils:
# * missing-plugins
# * install-plugins (and detect if there weren't compiled because of a version
#   of plugins-base too low)

