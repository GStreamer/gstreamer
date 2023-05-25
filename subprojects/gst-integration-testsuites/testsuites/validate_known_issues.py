KNOWN_ISSUES = {
    "General flakiness": {
        "tests": [
            r"^validate\.((?!play_15s).)*$",
            r"^validate\.((?!transcode).)*$",
            r"^validate\.((?!media_check).)*$",
            r"^validate\.((?!compositor).)*$",
            r"^validate\.((?!glvideomixer).)*$",
            r"^validate\.((?!launch_pipeline).)*$",
            r"^validate\.((?!rtsp*).)*$",
            r"^validate\.((?!dash*).)*$",
        ],
        "max_retries": 2,
    },
    "https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/486": {
        "tests": [
            "validate.dash.playback.fast_forward.dash_exMPD_BIP_TC1",
            "validate.dash.playback.reverse_playback.dash_exMPD_BIP_TC1",
            "validate.dash.playback.seek_with_stop.dash_exMPD_BIP_TC1",
            "validate.dash.playback.scrub_forward_seeking.dash_exMPD_BIP_TC1",
            "validate.dash.playback.seek_backward.dash_exMPD_BIP_TC1",
            "validate.dash.playback.seek_forward.dash_exMPD_BIP_TC1",
        ],
        "issues": [
            {
                "detected-on": "playbin",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "sometimes": True,
            },
            {
                "summary": "flow return from pad push doesn't match expected value",
                "details": ".*Wrong combined flow return error.*",
                "level": "critical",
                "sometimes": True,
            },
            {
                "level": "critical",
                "summary": "The program stopped before some actions were executed",
                "sometimes": True,
            },
            {
                "timeout": True,
                "sometimes": True,
            },
        ]
    },
    "https://gitlab.freedesktop.org/gstreamer/gst-plugins-base/issues/311": {
        "tests": [
            "validate.http.*.ogg$",
            "validate.http.*.ogv$",
            "validate.rtsp.*.ogg$",
            "validate.rtsp.*.ogv$",
        ],
        "issues": [
            {
                "detected-on": "playbin",
                "summary": "We got an ERROR message on the bus",
                "details": ".*No valid frames decoded before end of stream.*",
                "level": "critical",
                "sometimes": True,
            },
            {
                "level": "critical",
                "summary": "We got an ERROR message on the bus",
                "details": ".*Got error: Could not decode stream.*",
                "sometimes": True,
            },
            {
                "level": "critical",
                "summary": "The program stopped before some actions were executed",
                "sometimes": True,
            },
            {
                "summary": "The program stopped before some actions were executed",
                "issue-id": "scenario::not-ended",
                "sometimes": True,
            },
        ]
    },
    "https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/563": {
        "tests": [
            "validate.rtsp.playback.seek_backward.bowlerhatdancer_sleepytom_SGP_mjpeg_avi",
        ],
        "issues": [
            {
                "level": "critical",
                "summary": "The program stopped before some actions were executed",
                "sometimes": True,
            }
        ]
    },
    "https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/377": {
        "tests": [
            "validate.rtsp.*",
        ],
        "issues": [
            {
                "timeout": True,
                "sometimes": True,
            },
        ]
    },
    # https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/930
    "WontFix-legacy-HLS-1": {
        "tests": [
            "validate.hls.playback.reverse_playback.*",
        ],
        "issues": [
            {
                'timeout': True,
                'sometimes': True,
            },
        ],
    },
    "https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/582": {
        "tests": [
            "validate.http.playback.reverse_playback.*",
            "validate.http.playback.*seek.*",
            "validate.http.playback.*change_state.*",
        ],
        "issues": [
            {
                'timeout': True,
                'sometimes': True,
            },
        ],
    },
    # https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/609
    "WontFix-legacy-HLS-2": {
        "tests": [
            "validate.hls.playback.*seek.*",
        ],
        "issues": [
            {
                'timeout': True,
                'sometimes': True,
                'stacktrace_symbols': [
                    'g_rec_mutex_lock'
                ]
            },
        ],
    },
    # https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/937
    "WontFix-legacy-HLS-3": {
        "tests": [
            "validate.hls.playback.fast_forward.*",
        ],
        "issues": [
            {
                'timeout': True,
                'sometimes': True,
            },
        ],
    },
    "https://gitlab.freedesktop.org/gstreamer/gst-plugins-base/issues/578": {
        "tests": [
            "validate.http.playback.change_state_intensive.*ogv",
            "validate.http.playback.change_state_intensive.*ogg",
        ],
        "issues": [
            {
                'timeout': True,
                'sometimes': True,
            },
        ],
    },
    "https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/585": {
        "tests": [
            "validate.rtsp.*playback.*seek.*.",
        ],
        "issues": [
            {
                "summary": "We got a g_log critical issue",
                "details": ".*g_hash_table_foreach_remove_or_steal.*",
            },
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "playbin0",
                "details": ".*Could not open resource for reading and writing.*",
            },
        ],
    },
    "https://gitlab.freedesktop.org/gstreamer/gst-plugins-base/issues/578": {
        "tests": [
            "validate.http.playback.change_state_intensive.*ogv",
            "validate.http.playback.change_state_intensive.*ogg",
        ],
        "issues": [
            {
                'timeout': True,
                'sometimes': True,
            },
        ],
    },
    "https://gitlab.freedesktop.org/gstreamer/gst-plugins-base/issues/579": {
        "tests": [
            "validate.http.*ogv",
            "validate.http.*ogg",
        ],
        "issues": [
            {
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "details": ".*Could not decode stream.*",
                "sometimes": True,
            },
        ]
    },
    "https://gitlab.freedesktop.org/gstreamer/gst-libav/issues/45": {
        "tests": [
            "validate.file.playback.reverse_playback.rawaudioS32LE_prores_mov"
        ],
        "issues": [
            {
                "issue-id": "runtime::error-on-bus",
                "summary": "We got an ERROR message on the bus",
                "level": "critical",
                "detected-on": "playbin0",
                "details": ".*No valid frames decoded before end of stream.*",
            },
            {
                "issue-id": "scenario::not-ended",
                "summary": "The program stopped before some actions were executed",
                "level": "critical",
                "detected-on": "reverse_playback",
            },
        ],
    },
    "Our asf file is basically broken": {
        "tests": [
            "validate.file.transcode.*.samples_multimedia_cx_asf_wmv_elephant_asf",
        ],
        "issues": [
            {
                "issue-id": "transcoded-file-wrong-duration",
            },
        ]
    },
}
