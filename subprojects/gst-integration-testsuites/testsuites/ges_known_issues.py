KNOWN_ISSUES = {
    'https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/4021': {
        'tests': [
            'ges.playback.scrub_forward_seeking.test_mixing.*mp3.*',
            'ges.scenario.add_effect_and_split',
        ],
        'issues': [
            {
                'summary': 'position after a seek is wrong',
                'sometimes': True,
            },
            {
                "summary": "The execution of an action did not properly happen",
                "issue-id": "scenario::execution-error",
            },
        ]
    },
    "https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/4020": {
        "tests": [
            "ges.playback.scrub_backward_seeking.test_transition.*"
        ],
        "issues": [
            {
                "issue-id": "event::segment-has-wrong-start",
                "summary": "A segment doesn't have the proper time value after an ACCURATE seek",
                "level": "critical",
                "can-happen-several-times": True,
            },
        ],
    },
    "General flakiness": {
        "tests": [
            r"^ges\.((?!render).)*$",
            r"^ges\.playback\.nested.*$",
        ],
        "max_retries": 2,
    },
}
