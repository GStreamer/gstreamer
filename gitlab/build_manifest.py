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


# Basically, pytest will happily let a test mutate a variable, and then run
# the next tests one the same environment without reset the vars.
def preserve_ci_vars(func):
    """Preserve the original CI Variable values"""
    def wrapper():
        try:
            url = os.environ["CI_PROJECT_URL"]
            user = os.environ["GITLAB_USER_LOGIN"]
        except KeyError:
            url = "invalid"
            user = ""

        private = os.getenv("READ_PROJECTS_TOKEN", default=None)
        if not private:
            os.environ["READ_PROJECTS_TOKEN"] = "FOO"

        func()

        os.environ["CI_PROJECT_URL"] = url
        os.environ["GITLAB_USER_LOGIN"] = user

        if private:
            os.environ["READ_PROJECTS_TOKEN"] = private
        # if it was set after
        elif os.getenv("READ_PROJECTS_TOKEN", default=None):
            del os.environ["READ_PROJECTS_TOKEN"]

    return wrapper


def request_raw(path: str, headers: Dict[str, str], project_url: str) -> List[Dict[str, str]]:
    base_url: str = get_hostname(project_url)
    url: str = f"https://{base_url}/api/v4/{path}"
    print(f"GET {url}")
    print(f"Headers: {headers}")
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
        print("Custom token was not set, group api querries will fail")
        # JOB_TOKEN is the default placeholder of CI_JOB_TOKEN
        headers: Dict[str, str] = {'JOB_TOKEN': "xxxxxxxxxxxxxxxxxxxx" }

    # mock: "https://gitlab.freedesktop.org/gstreamer/gstreamer"
    project_url: str = os.environ['CI_PROJECT_URL']
    return request_raw(path, headers, project_url)


def request_wrap(path: str) -> List[Dict[str, str]]:
    resp: List[Dict[str, str]] = request(path)

    if not resp:
        return None
    if not resp[0]:
        return None

    return resp[0]


def get_project_branch(project_id: int, name: str) -> Dict[str, str]:
    print(f"Searching for {name} branch in project {project_id}")
    path = f"projects/{project_id}/repository/branches?search={name}"
    return request_wrap(path)


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
    path = f"/users/{user}/projects?search={project}"
    return request_wrap(path)


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

# Documentation: https://docs.gitlab.com/ee/api/search.html#group-search-api
def search_group_namespace(group_id: str, project: str) -> Dict[str, str]:
    print(f"Searching for {project} project in @{group_id} group namespace")
    path = f"groups/{group_id}/search?scope=projects&search={project}"
    return request_wrap(path)


@preserve_ci_vars
def test_search_group_namespace():
    import pytest
    try:
        instance = get_hostname(os.environ['CI_PROJECT_URL'])
        # This tests need to authenticate with the gitlab instace,
        # and its hardcoded to check for the gitlab-org which exists
        # on gitlab.com
        # This can be changed to a gstreamer once its migrated to gitlab.fd.o
        if instance != "gitlab.freedesktop.org":
            # too lazy to use a different error
            raise KeyError
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
    user_login: str = os.environ["GITLAB_USER_LOGIN"]
    project = search_user_namespace(user_login, module)

    # Find a fork in the User's namespace
    if project:
        id = project['id']
        print(f"User project found, id: {id}")
        # If we have a branch with same name, use it.
        branch = get_project_branch(id, branchname)
        if branch is not None:
            path = project['namespace']['path']
            print("Found mathcing branch in user's namespace")
            print(f"{path}/{branchname}")

            return 'user', branch['commit']['id']
        print(f"Did not found user branch named {branchname}")

    # This won't work until gstreamer migrates to gitlab
    # Else check the upstream gstreamer repository
    project = search_group_namespace('gstreamer', module)
    if project:
        print(f"Project found in Gstreamer upstream, id: {id}")
        id = project['id']
        # If we have a branch with same name, use it.
        branch = get_project_branch(id, branchname)
        if branch is not None:
            print("Found matching branch in upstream gst repo")
            print(f"gstreamer/{branchname}")
            return 'gstreamer', branch['commit']['id']

        branch = get_project_branch(id, 'master')
        if branch is not None:
            # print("Falling back to master branch in upstream repo")
            print('gstreamer/master')
            return 'gstreamer', branch.attributes['commit']['id']

    print('Falling back to origin/master')
    return 'origin', 'master'


@preserve_ci_vars
def test_find_repository_sha():
    os.environ["CI_PROJECT_URL"] = "https://gitlab.freedesktop.org/gstreamer/gst-plugins-good"
    os.environ["GITLAB_USER_LOGIN"] = "alatiera"
    del os.environ["READ_PROJECTS_TOKEN"]

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
    user_remote_url: str = os.path.dirname(os.environ['CI_PROJECT_URL'])
    if not user_remote_url.endswith('/'):
        user_remote_url += '/'

    for module in GSTREAMER_MODULES:
        print(f"Checking {module}:", end=' ')
        current_branch: str = os.environ['CI_COMMIT_REF_NAME']
        remote, revision = find_repository_sha(module, current_branch)
        projects += project_template.format(module, remote, revision)

    with open('manifest.xml', mode='w') as manifest:
        print(MANIFEST_TEMPLATE.format(user_remote_url, projects), file=manifest)
