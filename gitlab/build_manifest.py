#!/usr/bin/env python3

import argparse
import os
import requests
import sys
import subprocess

from typing import Dict, Tuple, List
from urllib.parse import urlparse
# from pprint import pprint

# Each item is a Tuple of (project-path, project-id)
# ex. https://gitlab.freedesktop.org/gstreamer/gst-build
# has project path 'gst-build' and project-id '1342'
# TODO: Named tuples are awesome
GSTREAMER_MODULES: List[Tuple[str, int]] = [
    # ('orc', 1360),
    ('gst-build', 1342),
    ('gstreamer', 1357),
    ('gst-plugins-base', 1352),
    ('gst-plugins-good', 1353),
    ('gst-plugins-bad', 1351),
    ('gst-plugins-ugly', 1354),
    ('gst-libav', 1349),
    ('gst-devtools', 1344),
    ('gst-docs', 1345),
    ('gst-editing-services', 1346),
    ('gst-omx', 1350),
    ('gst-python', 1355),
    ('gst-rtsp-server', 1362),
    ('gstreamer-vaapi', 1359),
]

MANIFEST_TEMPLATE: str = """<?xml version="1.0" encoding="UTF-8"?>
<manifest>
  <remote fetch="{}" name="user"/>
  <remote fetch="https://gitlab.freedesktop.org/gstreamer/" name="origin"/>
{}
</manifest>"""


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


def request_raw(path: str, headers: Dict[str, str], project_url: str) -> List[Dict[str, str]]:
    # ex. base_url = "gitlab.freedesktop.org"
    base_url: str = urlparse(project_url).hostname
    url: str = f"https://{base_url}/api/v4/{path}"
    print(f"GET {url}")
    # print(f"Headers: {headers}")
    resp = requests.get(url, headers=headers)

    print(f"Request returned: {resp.status_code}")
    if not resp.ok:
        return None

    return resp.json()


def request(path: str) -> List[Dict[str, str]]:
    # Check if there is a custom token set
    # API calls to Group namespaces need to be authenticated
    # regardless if the group/projects are public or not.
    # CI_JOB_TOKEN has an actuall value only for private jobs
    # and that's also an Gitlab EE feature.
    # Which means no matter what we need to give the runner
    # an actuall token if we want to query even the public
    # gitlab.fd.o/gstreamer group
    try:
        headers: Dict[str, str] = {'Private-Token': os.environ["READ_PROJECTS_TOKEN"] }
    except KeyError:
        # print("Custom token was not set, group api querries will fail")
        # JOB_TOKEN is the default placeholder of CI_JOB_TOKEN
        headers: Dict[str, str] = {'JOB_TOKEN': "xxxxxxxxxxxxxxxxxxxx" }

    # mock: "https://gitlab.freedesktop.org/gstreamer/gstreamer"
    project_url: str = os.environ['CI_PROJECT_URL']
    return request_raw(path, headers, project_url)


def get_project_branch(project_id: int, name: str) -> Dict[str, str]:
    print(f"Searching for {name} branch in project {project_id}")
    path = f"projects/{project_id}/repository/branches?search={name}"
    results = request(path)

    if not results:
        return None

    # The api returns a list of projects that match the search
    # we want the exact match, which might not be the first on the list
    for project_res in results:
        if project_res["name"] == name:
            return project_res

    return None


@preserve_ci_vars
def test_get_project_branch():
    id = 1353
    os.environ["CI_PROJECT_URL"] = "https://gitlab.freedesktop.org/gstreamer/gst-plugins-good"
    del os.environ["READ_PROJECTS_TOKEN"]

    twelve = get_project_branch(id, '1.12')
    assert twelve is not None
    assert twelve['name'] == '1.12'

    fourteen = get_project_branch(id, '1.14')
    assert fourteen is not None
    assert fourteen['name'] == '1.14'

    failure = get_project_branch(id, 'why-would-anyone-chose-this-branch-name')
    assert failure is None

    failure2 = get_project_branch("invalid-id", '1.12')
    assert failure2 is None


# Documentation: https://docs.gitlab.com/ce/api/projects.html#list-user-projects
def search_user_namespace(user: str, project: str) -> Dict[str, str]:
    print(f"Searching for {project} project in @{user} user's namespace")
    path = f"users/{user}/projects?search={project}"
    results = request(path)

    if not results:
        return None

    # The api returns a list of projects that match the search
    # we want the exact match, which might not be the first on the list
    for project_res in results:
        if project_res["path"] == project:
            return project_res

    return None


@preserve_ci_vars
def test_search_user_namespace():
    os.environ["CI_PROJECT_URL"] = "https://gitlab.freedesktop.org/alatiera/gst-plugins-good"
    del os.environ["READ_PROJECTS_TOKEN"]
    user = "alatiera"

    gst = search_user_namespace("alatiera", "gstreamer")
    assert gst is not None
    assert gst['path'] == 'gstreamer'

    gst_good = search_user_namespace("alatiera", "gst-plugins-good")
    assert gst_good is not None
    assert gst_good['path'] == 'gst-plugins-good'

    res = search_user_namespace("alatiera", "404-project-not-found")
    assert res is None

    # Passing a group namespace instead of user should return None
    res = search_user_namespace("gstreamer", "gst-plugins-good")
    assert res is None


def find_repository_sha(module: Tuple[str, int], branchname: str) -> Tuple[str, str]:
    namespace: str = os.environ["CI_PROJECT_NAMESPACE"]

    if module[0] == os.environ['CI_PROJECT_NAME']:
        return 'user', os.environ['CI_COMMIT_SHA']

    if branchname != "master":
        project = search_user_namespace(namespace, module[0])
        # Find a fork in the User's namespace
        if project:
            id = project['id']
            print(f"User project found, id: {id}")
            # If we have a branch with same name, use it.
            branch = get_project_branch(id, branchname)
            if branch is not None:
                path = project['namespace']['path']
                print("Found matching branch in user's namespace")
                print(f"{path}/{branchname}")

                return 'user', branch['commit']['id']
            print(f"Did not find user branch named {branchname}")

    # Check upstream project for a branch
    branch = get_project_branch(module[1], branchname)
    if branch is not None:
        print("Found mathcing branch in upstream project")
        print(f"gstreamer/{branchname}")
        return 'origin', branch['commit']['id']

    # Fallback to using upstream master branch
    branch = get_project_branch(module[1], 'master')
    if branch is not None:
        print("Falling back to master branch on upstream project")
        print(f"gstreamer/master")
        return 'origin', branch['commit']['id']

    # This should never occur given the upstream fallback above
    print("If something reaches that point, please file a bug")
    print("https://gitlab.freedesktop.org/gstreamer/gst-ci/issues")
    assert False


@preserve_ci_vars
def test_find_repository_sha():
    os.environ["CI_PROJECT_URL"] = "https://gitlab.freedesktop.org/gstreamer/gst-plugins-good"
    os.environ["CI_PROJECT_NAMESPACE"] = "alatiera"
    del os.environ["READ_PROJECTS_TOKEN"]

    # This should find the repository in the user namespace
    remote, git_ref = find_repository_sha(("gst-plugins-good", 1353), "1.2")
    assert remote == "user"
    assert git_ref == "08ab260b8a39791e7e62c95f4b64fd5b69959325"

    # This should fallback to upstream master branch since no matching branch was found
    remote, git_ref = find_repository_sha(("gst-plugins-good", 1353), "totally-valid-branch-name")
    assert remote == "origin"

    # This should fallback to upstream master branch since no repository was found
    remote, git_ref = find_repository_sha(("totally-valid-project-name", 42), "1.2")
    assert remote == "origin"
    # This is now the sha of the last commit
    # assert git_ref == "master"

    os.environ["CI_PROJECT_NAME"] = "the_project"
    os.environ["CI_COMMIT_SHA"] = "MySha"

    remote, git_ref = find_repository_sha(("the_project", 199), "whatever")
    assert remote == "user"
    assert git_ref == "MySha"


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
        remote, sha = find_repository_sha(("gst-ci", "1343"), current_branch)
        if remote == 'user':
            remote = user_remote_url + 'gst-ci'
        else:
            remote = "https://gitlab.freedesktop.org/gstreamer/gst-ci"

        subprocess.check_call(['git', 'fetch', remote, sha])
        subprocess.check_call(['git', 'checkout', '--detach', 'FETCH_HEAD'])
        sys.exit(0)

    projects: str = ''
    project_template: str = "  <project name=\"{}\" remote=\"{}\" revision=\"{}\" />\n"
    for module in GSTREAMER_MODULES:
        print(f"Checking {module}:", end=' ')
        remote, revision = find_repository_sha(module, current_branch)
        projects += project_template.format(module[0], remote, revision)

    with open(options.output, mode='w') as manifest:
        print(MANIFEST_TEMPLATE.format(user_remote_url, projects), file=manifest)
