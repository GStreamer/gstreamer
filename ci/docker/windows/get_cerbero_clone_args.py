#!/usr/bin/python3

import os
import gitlab


server = 'https://gitlab.freedesktop.org'
gl = gitlab.Gitlab(server)
branch = os.environ.get('DEFAULT_BRANCH', 'main')
project = f'gstreamer/cerbero'
# We do not want to run on (often out of date) user upstream branch
if os.environ["CI_COMMIT_REF_NAME"] != os.environ['DEFAULT_BRANCH']:
    try:
        try_project = f'{os.environ["CI_PROJECT_NAMESPACE"]}/cerbero'
        match_branch = os.environ["CI_COMMIT_REF_NAME"]
        # Search for matching branches, return only if the branch name matches
        # exactly
        proj = gl.projects.get(try_project)
        for b in proj.branches.list(search=match_branch, iterator=True):
            if match_branch == b.name:
                project = try_project
                branch = b.name
                break
    except gitlab.exceptions.GitlabGetError:
        pass

print(f'-b {branch} {server}/{project}', end='')
