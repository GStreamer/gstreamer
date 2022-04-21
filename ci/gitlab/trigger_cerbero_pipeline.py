#!/usr/bin/python3

import time
import os
import sys
import gitlab

CERBERO_PROJECT = 'gstreamer/cerbero'


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
    server = os.environ['CI_SERVER_URL']
    gl = gitlab.Gitlab(server,
                       private_token=os.environ.get('GITLAB_API_TOKEN'),
                       job_token=os.environ.get('CI_JOB_TOKEN'))

    cerbero = None
    # We do not want to run on (often out of date) user upstream branch
    if os.environ["CI_COMMIT_REF_NAME"] != os.environ['GST_UPSTREAM_BRANCH']:
        try:
            user_cerbero_name = f'{os.environ["CI_PROJECT_NAMESPACE"]}/cerbero'
            cerbero = gl.projects.get(user_cerbero_name)
            if os.environ["CI_COMMIT_REF_NAME"] in [b.name for b in cerbero.branches.list()]:
                cerbero_branch = os.environ["CI_COMMIT_REF_NAME"]
                fprint(
                    f"-> Triggering on branch {cerbero_branch} in {user_cerbero_name}\n")
            else:
                # No branch with a same name on the user cerbero repo... trigger
                # on upstream project
                cerbero = None
        except gitlab.exceptions.GitlabGetError:
            pass

    if cerbero is None:
        cerbero = gl.projects.get(CERBERO_PROJECT)
        cerbero_branch = os.environ["GST_UPSTREAM_BRANCH"]

    pipe = cerbero.trigger_pipeline(
        token=os.environ['CI_JOB_TOKEN'],
        ref=cerbero_branch,
        variables={
            "CI_GSTREAMER_URL": os.environ["CI_PROJECT_URL"],
            "CI_GSTREAMER_REF_NAME": os.environ["CI_COMMIT_REF_NAME"],
            # This tells cerbero CI that this is a pipeline started via the
            # trigger API, which means it can use a deps cache instead of
            # building from scratch.
            "CI_GSTREAMER_TRIGGERED": "true",
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
