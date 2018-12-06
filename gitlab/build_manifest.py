#!/usr/bin/env python3

import argparse
import os
import requests
import sys
import subprocess

from typing import Dict, Tuple, List
from urllib.parse import urlparse
# from pprint import pprint

GSTREAMER_MODULES: List[str] = [
    # 'orc',
    'gst-build',
    'gstreamer',
    'gst-plugins-base',
    'gst-plugins-good',
    'gst-plugins-bad',
    'gst-plugins-ugly',
    'gst-libav',
    'gst-devtools',
    'gst-docs',
    'gst-editing-services',
    'gst-omx',
    'gst-python',
    'gst-rtsp-server',
    'gstreamer-vaapi',
]

MANIFEST_TEMPLATE: str = """<?xml version="1.0" encoding="UTF-8"?>
<manifest>
  <remote fetch="{}" name="user"/>
  <remote fetch="https://gitlab.freedesktop.org/gstreamer/" name="origin"/>
{}
</manifest>"""


def git(*args, repository_path='.'):
    return subprocess.check_output(["git"] + list(args), cwd=repository_path,
                                   ).decode()


def get_repository_sha_in_namespace(module: str, namespace: str, branches: List[str]) -> str:
    print(branches)
    res = git('ls-remote', f'https://gitlab.freedesktop.org/{namespace}/{module}.git', *branches)
    if not res:
        return None

    for branch in branches:
        for line in res.split('\n'):
            if line.endswith('/' + branch):
                return res.split('\t')[0]


def find_repository_sha(module: str, branchname: str) -> Tuple[str, str]:
    namespace: str = os.environ["CI_PROJECT_NAMESPACE"]

    if module == os.environ['CI_PROJECT_NAME']:
        return 'user', os.environ['CI_COMMIT_SHA']

    if branchname != "master":
        sha = get_repository_sha_in_namespace(module, namespace, [branchname])
        if sha is not None:
            return 'user', sha

        print(f"Did not find user branch named {branchname}")

    # Check upstream project for a branch
    sha = get_repository_sha_in_namespace(module, 'gstreamer', [branchname, 'master'])
    if sha is not None:
        print("Found mathcing branch in upstream project")
        print(f"gstreamer/{branchname}")
        return 'origin', sha

    # This should never occur given the upstream fallback above
    print(f"Could not find anything for {module}:{branchname}")
    print("If something reaches that point, please file a bug")
    print("https://gitlab.freedesktop.org/gstreamer/gst-ci/issues")
    assert False


# --- Unit tests --- #
# Basically, pytest will happily let a test mutate a variable, and then run
# the next tests one the same environment without reset the vars.
def preserve_ci_vars(func):
    """Preserve the original CI Variable values"""
    def wrapper():
        try:
            url = os.environ["CI_PROJECT_URL"]
            user = os.environ["CI_PROJECT_NAMESPACE"]
        except KeyError:
            url = "invalid"
            user = ""

        private = os.getenv("READ_PROJECTS_TOKEN", default=None)
        if not private:
            os.environ["READ_PROJECTS_TOKEN"] = "FOO"

        func()

        os.environ["CI_PROJECT_URL"] = url
        os.environ["CI_PROJECT_NAMESPACE"] = user

        if private:
            os.environ["READ_PROJECTS_TOKEN"] = private
        # if it was set after
        elif os.getenv("READ_PROJECTS_TOKEN", default=None):
            del os.environ["READ_PROJECTS_TOKEN"]

    return wrapper

@preserve_ci_vars
def test_find_repository_sha():
    os.environ["CI_PROJECT_URL"] = "https://gitlab.freedesktop.org/gstreamer/gst-plugins-good"
    os.environ["CI_PROJECT_NAMESPACE"] = "alatiera"
    del os.environ["READ_PROJECTS_TOKEN"]

    # This should find the repository in the user namespace
    remote, git_ref = find_repository_sha("gst-plugins-good", "1.2")
    assert remote == "user"
    assert git_ref == "08ab260b8a39791e7e62c95f4b64fd5b69959325"

    # This should fallback to upstream master branch since no matching branch was found
    remote, git_ref = find_repository_sha("gst-plugins-good", "totally-valid-branch-name")
    assert remote == "origin"

    # This should fallback to upstream master branch since no repository was found
    remote, git_ref = find_repository_sha("totally-valid-project-name", "1.2")
    assert remote == "origin"
    # This is now the sha of the last commit
    # assert git_ref == "master"

    os.environ["CI_PROJECT_NAME"] = "the_project"
    os.environ["CI_COMMIT_SHA"] = "MySha"

    remote, git_ref = find_repository_sha("the_project", "whatever")
    assert remote == "user"
    assert git_ref == "MySha"


@preserve_ci_vars
def test_get_project_branch():
    os.environ["CI_PROJECT_URL"] = "https://gitlab.freedesktop.org/gstreamer/gst-plugins-good"
    os.environ["CI_PROJECT_NAMESPACE"] = "nowaythisnamespaceexists_"
    del os.environ["READ_PROJECTS_TOKEN"]

    remote, twelve = find_repository_sha('gst-plugins-good', '1.12')
    assert twelve is not None
    assert remote == 'origin'

    remote, fourteen = find_repository_sha('gst-plugins-good', '1.12')
    assert fourteen is not None
    assert remote == 'origin'


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-update", action="store_true", default=False)
    parser.add_argument(dest="output", default='manifest.xml', nargs='?')
    options = parser.parse_args()

    current_branch: str = os.environ['CI_COMMIT_REF_NAME']
    user_remote_url: str = os.path.dirname(os.environ['CI_PROJECT_URL'])
    if not user_remote_url.endswith('/'):
        user_remote_url += '/'

    if options.self_update:
        remote, sha = find_repository_sha("gst-ci", current_branch)
        if remote == 'user':
            remote = user_remote_url + 'gst-ci'
        else:
            remote = "https://gitlab.freedesktop.org/gstreamer/gst-ci"

        git('fetch', remote, sha)
        git('checkout', '--detach', 'FETCH_HEAD')
        sys.exit(0)

    projects: str = ''
    project_template: str = "  <project path=\"%(name)s\" name=\"%(name)s.git\" remote=\"%(remote)s\" revision=\"%(revision)s\" />\n"
    for module in GSTREAMER_MODULES:
        print(f"Checking {module}:", end=' ')
        remote, revision = find_repository_sha(module, current_branch)
        projects += project_template % {'name': module, 'remote': remote, 'revision': revision}

    with open(options.output, mode='w') as manifest:
        print(MANIFEST_TEMPLATE.format(user_remote_url, projects), file=manifest)
