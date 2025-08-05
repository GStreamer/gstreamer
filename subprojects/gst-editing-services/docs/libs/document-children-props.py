#!/usr/bin/env python3

"""
Simple script to update the children properties information for
GESTrackElement-s that add children properties all the time
"""

import os
import textwrap
from itertools import chain

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GObject", "2.0")
gi.require_version("GES", "1.0")

from gi.repository import Gst, GES, GObject  # noqa: E402

overrides = {
    "GstFramePositioner": False,
    "GstBaseTextOverlay": "GstBaseTextOverlay",  # Use the actual class name for proper linking
    "GstVideoDirection": "GstVideoDirection",     # Use the actual interface name for proper linking
    "GESVideoTestSource": "GESVideoTestSource",
    "GESVideoTransition": "GESVideoTransition",
}


def get_enum_values_from_property(prop):
    """
    Get enum values using proper introspection for newer pygobject versions.
    """
    enum_class = prop.enum_class

    # Get the enum type from the default value
    try:
        default_val = prop.get_default_value()
        enum_type = type(default_val)
    except Exception:
        # Fallback: create enum values as integers
        return [(i, f"Value_{i}", f"value-{i}") for i in range(enum_class.n_values)]

    values = []

    # In some cases there are 'gaps' like in the GstBaseTextOverlayHAlign where
    # value 3 is GST_BASE_TEXT_OVERLAY_HALIGN_UNUSED - so we need to compensate
    # for the gap
    n_values = enum_class.n_values
    for i in range(n_values):
        try:
            enum_type(i)
        except Exception:
            n_values += 1

    # Get all enum values by creating them with integer values
    for i in range(n_values):
        try:
            # For other values, create them using the enum type
            enum_val = enum_type(i)
            values.append((i, enum_val.value_name, enum_val.value_nick))
        except Exception:
            # Fallback for any failed value
            if i == 0:
                # Try to get first value info
                try:
                    first_val = enum_class.values
                    values.append((first_val.value, first_val.value_name, first_val.value_nick))
                except Exception:
                    continue
            else:
                continue

    return values


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

    add_clip(GES.UriClipAsset.request_sync(Gst.filename_to_uri(
        os.path.join("../../", "tests/check/assets/audio_video.ogg"))).extract())
    add_clip(GES.TestClip.new())
    add_clip(GES.TitleClip.new())

    add_clip(GES.SourceClip.new_time_overlay(), False, "GESTimeOverlaySourceClip")
    add_clip(GES.TransitionClip.new_for_nick("crossfade"), False)

    for element in elements:
        if isinstance(element, tuple):
            element, gtype = element
        else:
            gtype = element.__gtype__.name
        with open(gtype + '-children-props.md', 'w') as f:
            for prop in GES.TimelineElement.list_children_properties(element):
                prefix = '#### `%s`\n\n' % (prop.name)

                prefix_len = len(prefix)
                lines = [i for i in chain.from_iterable([textwrap.wrap(t, width=80) for t in prop.blurb.split('\n')])]

                doc = prefix + lines[0]

                if GObject.type_is_a(prop, GObject.ParamSpecEnum.__gtype__):
                    lines += ["", "Valid values:"]
                    try:
                        # Try the deprecated method first for backward compatibility
                        for value in prop.enum_class.__enum_values__.values():
                            lines.append("  - **%s** (%d) – %s" % (value.value_name,
                                                                   int(value), value.value_nick))
                    except AttributeError:
                        # Handle newer pygobject versions - use proper introspection
                        enum_values = get_enum_values_from_property(prop)
                        for value, name, nick in enum_values:
                            lines.append("  - **%s** (%d) – %s" % (name, value, nick))
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
