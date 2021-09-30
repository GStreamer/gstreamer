#!/usr/bin/python3
import os
import gitlab
from datetime import datetime
import tempfile
from subprocess import check_call, call, check_output

BRANCH="main"
NAMESPACE="gstreamer"
JOB="documentation"
DOC_BASE="/srv/gstreamer.freedesktop.org/public_html/documentation"

print(f"Running at {datetime.now()}")
with tempfile.TemporaryDirectory() as tmpdir:
    os.chdir(tmpdir)

    gl = gitlab.Gitlab("https://gitlab.freedesktop.org/")
    project = gl.projects.get(1357)
    pipelines = project.pipelines.list()
    for pipeline in pipelines:
        if pipeline.ref != BRANCH:
            continue

        job, = [j for j in pipeline.jobs.list() if j.name == "documentation"]
        if job.status != "success":
            continue

        url = f"https://gitlab.freedesktop.org/gstreamer/gstreamer/-/jobs/{job.id}/artifacts/download"
        print("============================================================================================================================")
        print(f"Updating documentation from: {url}\n\n")
        check_call(f"wget {url} -O gstdocs.zip", shell=True)
        print("Unziping file.")
        check_output("unzip gstdocs.zip", shell=True)
        print("Running rsync.")
        call(f"rsync -rvaz --links --delete documentation/ {DOC_BASE}", shell=True)
        call(f"chmod -R g+w {DOC_BASE}; chgrp -R gstreamer {DOC_BASE}", shell=True)

        print(f"Done updating doc")
        break
