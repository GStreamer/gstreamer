#!/usr/bin/python3

import time
import os
import sys
import gitlab

CERBERO_ID = 1340


class Status:
    FAILED = 'failed'
    MANUAL = 'manual'
    CANCELED = 'canceled'
    SUCCESS = 'success'
    SKIPPED = 'skipped'
    CREATED = 'created'

    @classmethod
    def is_finished(cls, state):
        return state in [
            cls.FAILED,
            cls.MANUAL,
            cls.CANCELED,
            cls.SUCCESS,
            cls.SKIPPED,
        ]


def fprint(msg):
    print(msg, end="")
    sys.stdout.flush()


if __name__ == "__main__":
    gl = gitlab.Gitlab(
        "https://gitlab.freedesktop.org/",
        private_token=os.environ.get('GITLAB_API_TOKEN'),
        job_token=os.environ.get('CI_JOB_TOKEN')
    )

    cerbero = gl.projects.get(CERBERO_ID)
    pipe = cerbero.trigger_pipeline(
        token=os.environ['CI_JOB_TOKEN'],
        ref=os.environ["GST_UPSTREAM_BRANCH"],
        variables={
            "CI_GSTREAMER_URL": os.environ["CI_PROJECT_URL"],
            "CI_GSTREAMER_REF_NAME": os.environ["CI_COMMIT_REF_NAME"],
        }
    )

    fprint(f'Cerbero pipeline running at {pipe.web_url} ')
    while True:
        time.sleep(15)
        pipe.refresh()
        if Status.is_finished(pipe.status):
            fprint(f": {pipe.status}\n")
            sys.exit(0 if pipe.status == Status.SUCCESS else 1)
        else:
            fprint(".")
