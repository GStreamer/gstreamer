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


# Disallow git prompting for a username/password
os.environ['GIT_TERMINAL_PROMPT'] = '0'
def git(*args, repository_path='.'):
    return subprocess.check_output(["git"] + list(args), cwd=repository_path).decode()


def get_branches_info(module: str, namespace: str, branches: List[str]) -> Tuple[str, str]:
    try:
        res = git('ls-remote', f'https://gitlab.freedesktop.org/{namespace}/{module}.git', *branches)
    except subprocess.CalledProcessError:
        return None, None

    if not res:
        return None, None

    lines = res.split('\n')
    for branch in branches:
        for line in lines:
            if line.endswith('/' + branch):
                try:
                    sha, refname = line.split('\t')
                except ValueError:
                    continue
                return refname.strip(), sha

    return None, None


def find_repository_sha(module: str, branchname: str) -> Tuple[str, str, str]:
    namespace: str = os.environ["CI_PROJECT_NAMESPACE"]

    if module == os.environ['CI_PROJECT_NAME']:
        return 'user', branchname, os.environ['CI_COMMIT_SHA']

    if branchname != "master":
        remote_refname, sha = get_branches_info(module, namespace, [branchname])
        if sha is not None:
            return 'user', remote_refname, sha

    # Check upstream project for a branch
    remote_refname, sha = get_branches_info(module, 'gstreamer', [branchname, 'master'])
    if sha is not None:
        return 'origin', remote_refname, sha

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
    os.environ["CI_PROJECT_NAME"] = "some-random-project"
    os.environ["CI_PROJECT_URL"] = "https://gitlab.freedesktop.org/gstreamer/gst-plugins-good"
    os.environ["CI_PROJECT_NAMESPACE"] = "alatiera"
    del os.environ["READ_PROJECTS_TOKEN"]

    # This should find the repository in the user namespace
    remote, refname, git_ref = find_repository_sha("gst-plugins-good", "1.2")
    assert remote == "user"
    assert git_ref == "08ab260b8a39791e7e62c95f4b64fd5b69959325"
    assert refname == "refs/heads/1.2"

    # This should fallback to upstream master branch since no matching branch was found
    remote, refname, git_ref = find_repository_sha("gst-plugins-good", "totally-valid-branch-name")
    assert remote == "origin"
    assert refname == "refs/heads/master"

    os.environ["CI_PROJECT_NAME"] = "the_project"
    os.environ["CI_COMMIT_SHA"] = "MySha"

    remote, refname, git_ref = find_repository_sha("the_project", "whatever")
    assert remote == "user"
    assert git_ref == "MySha"
    assert refname == "whatever"


@preserve_ci_vars
def test_get_project_branch():
    os.environ["CI_PROJECT_NAME"] = "some-random-project"
    os.environ["CI_COMMIT_SHA"] = "dwbuiw"
    os.environ["CI_PROJECT_URL"] = "https://gitlab.freedesktop.org/gstreamer/gst-plugins-good"
    os.environ["CI_PROJECT_NAMESPACE"] = "nowaythisnamespaceexists_"
    del os.environ["READ_PROJECTS_TOKEN"]

    remote, refname, twelve = find_repository_sha('gst-plugins-good', '1.12')
    assert twelve is not None
    assert remote == 'origin'
    assert refname == "refs/heads/1.12"

    remote, refname, fourteen = find_repository_sha('gst-plugins-good', '1.14')
    assert fourteen is not None
    assert remote == 'origin'
    assert refname == "refs/heads/1.14"


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
        remote, remote_refname, sha = find_repository_sha("gst-ci", current_branch)
        if remote == 'user':
            remote = user_remote_url + 'gst-ci'
        else:
            remote = "https://gitlab.freedesktop.org/gstreamer/gst-ci"

        git('fetch', remote, remote_refname)
        git('checkout', '--detach', sha)
        sys.exit(0)

    projects: str = ''
    for module in GSTREAMER_MODULES:
        print(f"Checking {module}:", end=' ')
        remote, refname, revision = find_repository_sha(module, current_branch)
        print(f"remote '{remote}', refname: '{refname}', revision: '{revision}'")
        projects += f"  <project path=\"{module}\" name=\"{module}.git\" remote=\"{remote}\" revision=\"{revision}\" refname=\"{refname}\" />\n"

    with open(options.output, mode='w') as manifest:
        print(MANIFEST_TEMPLATE.format(user_remote_url, projects), file=manifest)
