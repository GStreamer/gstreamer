#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# Copyright (C) 2019 Igalia S.L
# Authors:
#   Thibault Saunier <tsaunier@igalia.com>
#

import sys

import gi
import tempfile
gi.require_version("GES", "1.0")
gi.require_version("Gst", "1.0")

from gi.repository import GObject
from gi.repository import Gst
Gst.init(None)
from gi.repository import GES
from gi.repository import GLib
from collections import OrderedDict

import opentimelineio as otio
otio.adapters.from_name('xges')

class GESOtioFormatter(GES.Formatter):
    def do_save_to_uri(self, timeline, uri, overwrite):
        if not Gst.uri_is_valid(uri) or Gst.uri_get_protocol(uri) != "file":
            Gst.error("Protocol not supported for file: %s" % uri)
            return False

        with tempfile.NamedTemporaryFile(suffix=".xges") as tmpxges:
            timeline.get_asset().save(timeline, "file://" + tmpxges.name, None, overwrite)

            linker = otio.media_linker.MediaLinkingPolicy.ForceDefaultLinker
            otio_timeline = otio.adapters.read_from_file(tmpxges.name, "xges", media_linker_name=linker)
            location = Gst.uri_get_location(uri)
            out_adapter = otio.adapters.from_filepath(location)
            otio.adapters.write_to_file(otio_timeline, Gst.uri_get_location(uri), out_adapter.name)

        return True

    def do_can_load_uri(self, uri):
        try:
            if not Gst.uri_is_valid(uri) or Gst.uri_get_protocol(uri) != "file":
                return False
        except GLib.Error as e:
            Gst.error(str(e))
            return False

        if uri.endswith(".xges"):
            return False

        try:
            return otio.adapters.from_filepath(Gst.uri_get_location(uri)) is not None
        except Exception as e:
            Gst.info("Could not load %s -> %s" % (uri, e))
            return False


    def do_load_from_uri(self, timeline, uri):
        location = Gst.uri_get_location(uri)
        in_adapter = otio.adapters.from_filepath(location)
        assert(in_adapter) # can_load_uri should have ensured it is loadable

        linker = otio.media_linker.MediaLinkingPolicy.ForceDefaultLinker
        otio_timeline = otio.adapters.read_from_file(
            location,
            in_adapter.name,
            media_linker_name=linker
        )

        with tempfile.NamedTemporaryFile(suffix=".xges") as tmpxges:
            otio.adapters.write_to_file(otio_timeline, tmpxges.name, "xges")
            formatter = GES.Formatter.get_default().extract()
            timeline.get_asset().add_formatter(formatter)
            return formatter.load_from_uri(timeline, "file://" + tmpxges.name)

GObject.type_register(GESOtioFormatter)
known_extensions_mimetype_map = [
    ("otio", "xml", "fcpxml"),
    ("application/vnd.pixar.opentimelineio+json", "application/vnd.apple-xmeml+xml", "application/vnd.apple-fcp+xml")
]

extensions = []
for adapter in otio.plugins.ActiveManifest().adapters:
    if adapter.name != 'xges':
        extensions.extend(adapter.suffixes)

extensions_mimetype_map = [[], []]
for i, ext in enumerate(known_extensions_mimetype_map[0]):
    if ext in extensions:
        extensions_mimetype_map[0].append(ext)
        extensions_mimetype_map[1].append(known_extensions_mimetype_map[1][i])
        extensions.remove(ext)
extensions_mimetype_map[0].extend(extensions)

GES.FormatterClass.register_metas(GESOtioFormatter, "otioformatter",
    "GES Formatter using OpenTimelineIO",
    ','.join(extensions_mimetype_map[0]),
    ';'.join(extensions_mimetype_map[1]), 0.1, Gst.Rank.SECONDARY)
