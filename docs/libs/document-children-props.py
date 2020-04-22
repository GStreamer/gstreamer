#!/usr/bin/env python3

"""
Simple script to update the children properties information for
GESTrackElement-s that add children properties all the time
"""

import gi
import os
import sys
import textwrap

gi.require_version("Gst", "1.0")
gi.require_version("GObject", "2.0")
gi.require_version("GES", "1.0")

from gi.repository import Gst, GES, GObject

overrides = {
    "GstFramePositioner": False,
    "GstBaseTextOverlay": "timeoverlay",
    "GstVideoDirection": "videoflip",
    "GESVideoTestSource": "GESVideoTestSource",
    "GESVideoTransition": "GESVideoTransition",
}

if __name__ == "__main__":
    Gst.init(None)
    GES.init()

    os.chdir(os.path.realpath(os.path.dirname(__file__)))
    tl = GES.Timeline.new_audio_video()
    layer = tl.append_layer()

    elements = []
    def add_clip(c, add=True, override_name=None):
        c.props.duration = Gst.SECOND
        c.props.start = layer.get_duration()
        layer.add_clip(c)
        if add:
            elements.extend(c.children)
        else:
            if override_name:
                elements.append((c, override_name))
            else:
                elements.append(c)

    add_clip(GES.UriClipAsset.request_sync(Gst.filename_to_uri(os.path.join("../../", "tests/check/assets/audio_video.ogg"))).extract())
    add_clip(GES.TestClip.new())
    add_clip(GES.TitleClip.new())

    add_clip(GES.SourceClip.new_time_overlay(), False, "GESTimeOverlaySourceClip")
    add_clip(GES.TransitionClip.new_for_nick("crossfade"), False)

    for element in elements:
        if isinstance(element, tuple):
            element, gtype = element
        else:
            gtype = element.__gtype__.name
        print(gtype)
        with open(gtype + '-children-props.md', 'w') as f:
            for prop in GES.TimelineElement.list_children_properties(element):
                prefix = '#### `%s`\n\n' % (prop.name)

                prefix_len = len(prefix)
                lines = textwrap.wrap(prop.blurb, width=80)

                doc = prefix + lines[0]

                if GObject.type_is_a(prop, GObject.ParamSpecEnum.__gtype__):
                    lines += ["", "Valid values:"]
                    for value  in prop.enum_class.__enum_values__.values():
                        lines.append("  - **%s** (%d) – %s" % (value.value_name,
                            int(value), value.value_nick))
                else:
                    lines += ["", "Value type: #" + prop.value_type.name]

                typename = overrides.get(prop.owner_type.name, None)
                if typename is not False:
                    if typename is None:
                        if GObject.type_is_a(prop.owner_type, Gst.Element):
                            typename = GObject.new(prop.owner_type).get_factory().get_name()
                    lines += ["", "See #%s:%s" % (typename, prop.name)]

                if len(lines) > 1:
                    doc += '\n'
                    doc += '\n'.join(lines[1:])


                print(doc + "\n", file=f)
