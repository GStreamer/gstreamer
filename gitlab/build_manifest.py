#!/usr/bin/env python3

import os
import requests
import sys

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
    'gst-rtsp-server'
]

MANIFEST_TEMPLATE: str = """<?xml version="1.0" encoding="UTF-8"?>
<manifest>
  <remote fetch="{}" name="user"/>
  <remote fetch="https://gitlab.freedesktop.org/gstreamer/" name="gstreamer"/>
  <remote fetch="git://anongit.freedesktop.org/gstreamer/" name="origin"/>
{}
</manifest>"""


# Predefined variables by Gitlab-CI
# Documentation: https://docs.gitlab.com/ce/ci/variables/README.html#predefined-variables-environment-variables
#
# mock: "alatiera"
GITLAB_USER_LOGIN: str = os.environ["GITLAB_USER_LOGIN"]
# mock: "xxxxxxxxxxxxxxxxxxxx"
CI_TOKEN: str = os.environ["CI_JOB_TOKEN"]
# mock: "https://gitlab.freedesktop.org/gstreamer/gstreamer"
CI_PROJECT_URL: str = os.environ['CI_PROJECT_URL']
# mock: "foobar/a-branch-name"
CURRENT_BRANCH: str = os.environ['CI_COMMIT_REF_NAME']


def request_raw(path: str, token: str, project_url: str) -> List[Dict[str, str]]:
    gitlab_header: Dict[str, str] = {'JOB_TOKEN': token }
    base_url: str = get_hostname(project_url)
    url: str = f"https://{base_url}/api/v4/{path}"
    resp = requests.get(url, headers=gitlab_header)

    if not resp.ok:
        return None

    return resp.json()


def request(path: str) -> List[Dict[str, str]]:
    token = CI_TOKEN
    project_url = CI_PROJECT_URL
    return request_raw(path, token, project_url)


def get_project_branch(project_id: int, name: str) -> Dict[str, str]:
    path = f"projects/{project_id}/repository/branches?search={name}"
    resp: List[Dict[str, str]] = request(path)

    if not resp:
        return None
    if not resp[0]:
        return None

    # Not sure if there will be any edge cases where it returns more than one
    # so lets see if anyone complains
    assert len(resp) == 1
    return resp[0]


def test_get_project_branch():
    id = 1353
    os.environ["CI_JOB_TOKEN"] = "xxxxxxxxxxxxxxxxxxxx"
    os.environ["CI_PROJECT_URL"] = "https://gitlab.freedesktop.org/gstreamer/gst-plugins-good"

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
    path = f"/users/{user}/projects?search={project}"
    resp: List[Dict[str, str]] = request(path)

    if not resp:
        return None
    if not resp[0]:
        return None

    # Not sure if there will be any edge cases where it returns more than one
    # so lets see if anyone complains
    assert len(resp) == 1
    return resp[0]


def test_search_user_namespace():
    os.environ["CI_JOB_TOKEN"] = "xxxxxxxxxxxxxxxxxxxx"
    os.environ["CI_PROJECT_URL"] = "https://gitlab.freedesktop.org/alatiera/gst-plugins-good"
    user = "alatiera"

    gst = search_user_namespace("alatiera", "gstreamer")
    assert gst is not None
    assert gst['path'] == 'gstreamer'

    gst_good = search_user_namespace("alatiera", "gst-plugins-good")
    assert gst_good is not None
    assert gst_good['path'] == 'gst-plugins-good'

    res = search_user_namespace("alatiera", "404-project-not-found")
    assert res is None

# Documentation: https://docs.gitlab.com/ee/api/search.html#group-search-api
def search_group_namespace(group_id: str, project: str) -> Dict[str, str]:
    path = f"groups/{group_id}/search?scope=projects&search={project}"
    resp: List[Dict[str, str]] = request(path)

    if not resp:
        return None
    if not resp[0]:
        return None

    return resp[0]


def test_search_group_namespace():
    import pytest
    try:
        os.environ["CI_TOKEN"]
    except KeyError:
        pytest.skip("Need to be authenticated with gitlab to succed")

    os.environ["CI_PROJECT_URL"] = "https://gitlab.freedesktop.org/gstreamer/gstreamer"
    group = "gstreamer"

    lab = search_group_namespace(group, "gstreamer")
    assert lab is not None
    assert lab['path'] == 'gstreamer'

    res = search_user_namespace(group, "404-project-not-found")
    assert res is None


def get_hostname(url: str) -> str:
    return urlparse(url).hostname


def test_get_hostname():
    gitlab = 'https://gitlab.com/example/a_project'
    assert get_hostname(gitlab) == 'gitlab.com'

    fdo = 'https://gitlab.freedesktop.org/example/a_project'
    assert get_hostname(fdo) == 'gitlab.freedesktop.org'


def find_repository_sha(module: str, branchname: str) -> Tuple[str, str]:
    project = search_user_namespace(GITLAB_USER_LOGIN, module)

    # Find a fork in the User's namespace
    if project:
        id = project['id']
        # If we have a branch with same name, use it.
        branch = get_project_branch(id, branchname)
        if branch is not None:
            name = project['namespace']['path']
            print(f"{name}/{branchname}")

            return 'user', branch['commit']['id']

    # This won't work until gstreamer migrates to gitlab
    # Else check the upstream gstreamer repository
    project = search_group_namespace('gstreamer', module)
    if project:
        id = project['id']
        # If we have a branch with same name, use it.
        branch = get_project_branch(id, branchname)
        if branch is not None:
            print(f"gstreamer/{branchname}")
            return 'gstreamer', branch['commit']['id']

        branch = get_project_branch(id, 'master')
        if branch is not None:
            print('gstreamer/master')
            return 'gstreamer', branch.attributes['commit']['id']

    print('origin/master')
    return 'origin', 'master'


def test_find_repository_sha():
    os.environ["CI_JOB_TOKEN"] = "xxxxxxxxxxxxxxxxxxxx"
    os.environ["CI_PROJECT_URL"] = "https://gitlab.freedesktop.org/gstreamer/gst-plugins-good"
    os.environ["GITLAB_USER_LOGIN"] = "alatiera"

    # This should find the repository in the user namespace
    remote, git_ref = find_repository_sha("gst-plugins-good", "1.2")
    assert remote == "user"
    assert git_ref == "08ab260b8a39791e7e62c95f4b64fd5b69959325"

    # This should fallback to upstream master branch since no matching branch was found
    remote, git_ref = find_repository_sha("gst-plugins-good", "totally-valid-branch-name")
    assert remote == "origin"
    assert git_ref == "master"

    # This should fallback to upstream master branch since no repository was found
    remote, git_ref = find_repository_sha("totally-valid-project-name", "1.2")
    assert remote == "origin"
    assert git_ref == "master"


if __name__ == "__main__":
    projects: str = ''
    project_template: str = "  <project name=\"{}\" remote=\"{}\" revision=\"{}\" />\n"
    user_remote_url: str = os.path.dirname(CI_PROJECT_URL)
    if not user_remote_url.endswith('/'):
        user_remote_url += '/'

    for module in GSTREAMER_MODULES:
        print(f"Checking {module}:", end=' ')
        remote, revision = find_repository_sha(module, CURRENT_BRANCH)
        projects += project_template.format(module, remote, revision)

    with open('manifest.xml', mode='w') as manifest:
        print(MANIFEST_TEMPLATE.format(user_remote_url, projects), file=manifest)
