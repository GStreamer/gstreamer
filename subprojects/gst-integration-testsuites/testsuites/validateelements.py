# -*- Mode: Python -*- vi:si:et:sw=4:sts=4:ts=4:syntax=python
#
# Copyright (c) 2015,Thibault Saunier <thibault.saunier@collabora.com>
#               2015,Vineeth T M <vineeth.tm@samsung.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

"""
The GstValidate default testsuite
"""

import gi

gi.require_version("Gst", "1.0")

from gi.repository import Gst  # noqa
from gi.repository import GObject  # noqa

TEST_MANAGER = "validate"

KNOWN_ISSUES = {
    "validateelements.launch_pipeline.videocropbottom=2147483647.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.videocropbottom=2147483647.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::not-negotiated",
                "summary": "a NOT NEGOTIATED message has been posted on the bus.",
                "level": "critical",
                "detected-on": "pipeline0",
                "sometimes": False,
            }
        ]
    },
    "validateelements.launch_pipeline.videocropleft=2147483647.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.videocropleft=2147483647.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::not-negotiated",
                "summary": "a NOT NEGOTIATED message has been posted on the bus.",
                "level": "critical",
                "detected-on": "pipeline0",
                "sometimes": False,
            }
        ]
    },
    "validateelements.launch_pipeline.videocropright=2147483647.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.videocropright=2147483647.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::not-negotiated",
                "summary": "a NOT NEGOTIATED message has been posted on the bus.",
                "level": "critical",
                "detected-on": "pipeline0",
                "sometimes": False,
            }
        ]
    },
    "validateelements.launch_pipeline.videocroptop=2147483647.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.videocroptop=2147483647.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::not-negotiated",
                "summary": "a NOT NEGOTIATED message has been posted on the bus.",
                "level": "critical",
                "detected-on": "pipeline0",
                "sometimes": False,
            }
        ]
    },
    "validateelements.launch_pipeline.avwaitrecording=False.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.avwaitrecording=False.play_15s"
        ],
        "issues": [
            {
                "timeout": True,
                "sometimes": True,
            }
        ]
    },
    "validateelements.launch_pipeline.avwaitrecording=True.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.avwaitrecording=True.play_15s"
        ],
        "issues": [
            {
                "timeout": True,
                "sometimes": True,
            }
        ]
    },
    "validateelements.launch_pipeline.avwaittarget-running-time=0.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.avwaittarget-running-time=0.play_15s"
        ],
        "issues": [
            {
                "timeout": True,
                "sometimes": True,
            }
        ]
    },
    "validateelements.launch_pipeline.avwaittarget-running-time=18446744073709551615.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.avwaittarget-running-time=18446744073709551615.play_15s"
        ],
        "issues": [
            {
                "timeout": True,
                "sometimes": True,
            }
        ]
    },
    "validateelements.launch_pipeline.cvsmoothkernel-height=2147483647.play_15s issues": {
        "tests": ["validateelements.launch_pipeline.cvsmoothkernel-height=2147483647.play_15s issues"],
        "issues": [
            {
                "timeout": True,
                "sometimes": True,
            }
        ]
    },
    "validateelements.launch_pipeline.cvsmoothkernel-width=2147483647.play_15s issues": {
        "tests": ["validateelements.launch_pipeline.cvsmoothkernel-width=2147483647.play_15s issues"],
        "issues": [
            {
                "timeout": True,
                "sometimes": True,
            }
        ]
    },
    "validateelements.launch_pipeline.rawaudioparsenum-channels=2147483647.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.rawaudioparsenum-channels=2147483647.play_15s"
        ],
        "issues": [
            {
                "issue-id": "g-log::critical",
                "summary": "We got a g_log critical issue",
                "level": "critical",
                "detected-on": "pipeline0"
            },
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.rawvideoparseframe-size=4294967295.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.rawvideoparseframe-size=4294967295.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.rawvideoparseheight=0.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.rawvideoparseheight=0.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::not-negotiated",
                "summary": "a NOT NEGOTIATED message has been posted on the bus.",
                "level": "critical",
                "detected-on": "pipeline0"
            },
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.rawvideoparseheight=2147483647.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.rawvideoparseheight=2147483647.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::not-negotiated",
                "summary": "a NOT NEGOTIATED message has been posted on the bus.",
                "level": "critical",
                "detected-on": "pipeline0"
            },
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.rawvideoparsewidth=0.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.rawvideoparsewidth=0.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::not-negotiated",
                "summary": "a NOT NEGOTIATED message has been posted on the bus.",
                "level": "critical",
                "detected-on": "pipeline0"
            },
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.rawvideoparsewidth=2147483647.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.rawvideoparsewidth=2147483647.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::not-negotiated",
                "summary": "a NOT NEGOTIATED message has been posted on the bus.",
                "level": "critical",
                "detected-on": "pipeline0"
            },
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.shapewipeborder=0.0.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.shapewipeborder=0.0.play_15s"
        ],
        "issues": [
            {
                "timeout": True,
                "sometimes": True,
            }
        ]
    },
    "validateelements.launch_pipeline.shapewipeborder=1.0.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.shapewipeborder=1.0.play_15s"
        ],
        "issues": [
            {
                "timeout": True,
                "sometimes": True,
            }
        ]
    },
    "validateelements.launch_pipeline.shapewipeposition=0.0.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.shapewipeposition=0.0.play_15s"
        ],
        "issues": [
            {
                "timeout": True,
                "sometimes": True,
            }
        ]
    },
    "validateelements.launch_pipeline.shapewipeposition=1.0.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.shapewipeposition=1.0.play_15s"
        ],
        "issues": [
            {
                "timeout": True,
                "sometimes": True,
            }
        ]
    },
    "validateelements.launch_pipeline.videoraterate=0.0.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.videoraterate=0.0.play_15s"
        ],
        "issues": [
            {
                "timeout": True,
                "sometimes": True,
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspcompression-gain-db=0.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspcompression-gain-db=0.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspcompression-gain-db=9.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspcompression-gain-db=9.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspcompression-gain-db=90.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspcompression-gain-db=90.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspdelay-agnostic=False.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspdelay-agnostic=False.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspdelay-agnostic=True.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspdelay-agnostic=True.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspecho-cancel=True.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspecho-cancel=True.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspexperimental-agc=False.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspexperimental-agc=False.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspexperimental-agc=True.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspexperimental-agc=True.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspextended-filter=False.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspextended-filter=False.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspextended-filter=True.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspextended-filter=True.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspgain-control=False.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspgain-control=False.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspgain-control=True.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspgain-control=True.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdsphigh-pass-filter=False.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdsphigh-pass-filter=False.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdsphigh-pass-filter=True.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdsphigh-pass-filter=True.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdsplimiter=False.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdsplimiter=False.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdsplimiter=True.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdsplimiter=True.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspnoise-suppression=False.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspnoise-suppression=False.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspnoise-suppression=True.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspnoise-suppression=True.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspstartup-min-volume=12.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspstartup-min-volume=12.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspstartup-min-volume=255.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspstartup-min-volume=255.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdsptarget-level-dbfs=0.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdsptarget-level-dbfs=0.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdsptarget-level-dbfs=3.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdsptarget-level-dbfs=3.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdsptarget-level-dbfs=31.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdsptarget-level-dbfs=31.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspvoice-detection-frame-size-ms=10.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspvoice-detection-frame-size-ms=10.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspvoice-detection-frame-size-ms=30.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspvoice-detection-frame-size-ms=30.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspvoice-detection=False.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspvoice-detection=False.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.webrtcdspvoice-detection=True.play_15s issues": {
        "tests": [
            "validateelements.launch_pipeline.webrtcdspvoice-detection=True.play_15s"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "pipeline0"
            }
        ]
    },
    "validateelements.launch_pipeline.cameracalibratedelay=2147483647.play_15s issues": {
        "tests": ["validateelements.launch_pipeline.cameracalibratedelay=2147483647.play_15s issues"],
        "issues": [
            {
                "signame": "SIGIOT",
                "sometimes": True,
            }
        ]
    }
}


def pspec_is_numeric(prop):
    return prop.value_type in [GObject.TYPE_INT, GObject.TYPE_INT64,
                               GObject.TYPE_UINT, GObject.TYPE_UINT64,
                               GObject.TYPE_LONG, GObject.TYPE_ULONG,
                               GObject.TYPE_DOUBLE,
                               GObject.TYPE_FLOAT]


def get_pipe_and_populate(test_manager, klass, fname, prop, loop):
    prop_value = Gst.ElementFactory.make(fname, None).get_property(prop.name)

    if prop.value_type == GObject.TYPE_BOOLEAN:
        if loop is 1:
            bool_value = False
        else:
            bool_value = True
        cname = fname + " %s=%s" % (prop.name, bool_value)
        tname = fname + "%s=%s" % (prop.name, bool_value)
    elif pspec_is_numeric(prop):
        if loop is 2:
            int_value = prop.default_value
        elif loop is 1:
            int_value = prop.minimum
        else:
            int_value = prop.maximum
        cname = fname + " %s=%s" % (prop.name, int_value)
        tname = fname + "%s=%s" % (prop.name, int_value)
    else:
        cname = fname + " %s=%s" % (prop.name, prop_value)
        tname = fname + "%s=%s" % (prop.name, prop_value)

    if "Audio" in klass:
        cpipe = "audiotestsrc num-buffers=20 ! %s " % (cname)
        sink = "! audioconvert ! %(audiosink)s"
    elif "Video" in klass:
        if "gl" in fname:
            cname = "glfilterbin filter = %s" % (cname)
        cpipe = "videotestsrc num-buffers=20 ! %s " % (cname)
        sink = "! videoconvert ! %(videosink)s"
    else:
        return None

    if test_manager.options.mute:
        cpipe += "! fakesink"
    else:
        cpipe += "%s" % (sink)

    return (tname, cpipe)


def setup_tests(test_manager, options):
    print("Setting up tests to validate all elements")
    pipelines_descriptions = []
    test_manager.add_expected_issues(KNOWN_ISSUES)
    test_manager.set_default_blacklist([
        ("validateelements.launch_pipeline.videobox*",
         "Those are broken pipelines."),
        ("validateelements.launch_pipeline.frei0r*",
         "video filter plugins"),
        ("validateelements.launch_pipeline.smpte*",
         "smpte cannot be tested with simple pipeline. Hence excluding"),
        ("validateelements.launch_pipeline.glfilterbin*",
         "glfilter bin doesnt launch."),
        ("validateelements.launch_pipeline.audiomixmatrix*",
         "Now deprecated and requires specific properties to be set."),
    ])
    valid_scenarios = ["play_15s"]
    Gst.init(None)
    factories = Gst.Registry.get().get_feature_list(Gst.ElementFactory)
    for element_factory in factories:
        audiosrc = False
        audiosink = False
        videosrc = False
        videosink = False
        klass = element_factory.get_metadata("klass")
        fname = element_factory.get_name()

        if "Audio" not in klass and "Video" not in klass:
            continue

        padstemplates = element_factory.get_static_pad_templates()
        for padtemplate in padstemplates:
            if padtemplate.static_caps.string:
                caps = padtemplate.get_caps()
                for i in range(caps.get_size()):
                    structure = caps.get_structure(i)
                    if "audio/x-raw" in structure.get_name():
                        if padtemplate.direction == Gst.PadDirection.SRC:
                            audiosrc = True
                        elif padtemplate.direction == Gst.PadDirection.SINK:
                            audiosink = True
                    elif "video/x-raw" in structure.get_name():
                        if padtemplate.direction == Gst.PadDirection.SRC:
                            videosrc = True
                        elif padtemplate.direction == Gst.PadDirection.SINK:
                            videosink = True

        if (audiosink is False and videosink is False) or (audiosrc is False and videosrc is False):
            continue

        element = Gst.ElementFactory.make(fname, None)
        if element is None:
            print("Could not create element: %s" % fname)
            continue

        props = GObject.list_properties(element)
        for prop in props:
            if "name" in prop.name or "parent" in prop.name or "qos" in prop.name or \
               "latency" in prop.name or "message-forward" in prop.name:
                continue
            if (prop.flags & GObject.ParamFlags.WRITABLE) and \
               (prop.flags & GObject.ParamFlags.READABLE):
                if prop.value_type == GObject.TYPE_BOOLEAN:
                    loop = 2
                elif pspec_is_numeric(prop):
                    loop = 3
                else:
                    loop = 0

                while loop:
                    loop -= 1
                    description = get_pipe_and_populate(test_manager, klass,
                                                        fname, prop, loop)
                    if None is not description:
                        pipelines_descriptions.append(description)

    # No restriction about scenarios that are potentially used
    test_manager.add_scenarios(valid_scenarios)
    test_manager.add_generators(test_manager.GstValidatePipelineTestsGenerator
                                ("validate_elements", test_manager,
                                    pipelines_descriptions=pipelines_descriptions,
                                    valid_scenarios=valid_scenarios))

    return True
