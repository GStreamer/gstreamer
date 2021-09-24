#!/usr/bin/env python3

import os
import subprocess
import sys
from common import git


SCRIPTDIR = os.path.realpath(os.path.dirname(__file__))


if __name__ == "__main__":
    subprojects_dir = os.path.join(SCRIPTDIR, "..", "subprojects")
    exitcode = 0
    for repo_name in os.listdir(subprojects_dir):
        repo_dir = os.path.normpath(os.path.join(SCRIPTDIR, subprojects_dir, repo_name))
        if not os.path.exists(os.path.join(repo_dir, '.git')):
            continue

        diff = git('diff', repository_path=repo_dir).strip('\n')
        if diff:
            print('ERROR: Repository %s is not clean' % repo_dir)
            print('NOTE: Make sure to commit necessary changes in the gst_plugins_cache.json files')
            print(diff)
            exitcode += 1

    sys.exit(exitcode)