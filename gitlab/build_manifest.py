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


def request_raw(path: str, token: str, project_url: str) -> List[Dict[str, str]]:
    gitlab_header: Dict[str, str] = {'JOB_TOKEN': token }
    base_url: str = get_hostname(project_url)

    return requests.get(f"https://{base_url}/api/v4/" + path, headers=gitlab_header).json()


def request(path: str) -> List[Dict[str, str]]:
    token = os.environ["CI_JOB_TOKEN"]
    project_url = os.environ['CI_PROJECT_URL']
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


def get_hostname(url: str) -> str:
    return urlparse(url).hostname


def test_get_hostname():
    gitlab = 'https://gitlab.com/example/a_project'
    assert get_hostname(gitlab) == 'gitlab.com'

    fdo = 'https://gitlab.freedesktop.org/example/a_project'
    assert get_hostname(fdo) == 'gitlab.freedesktop.org'


def find_repository_sha(module: str, branchname: str) -> Tuple[str, str]:
    # FIXME: This does global search query in the whole gitlab instance.
    # It has been working so far by a miracle. It should be limited only to
    # the current namespace instead.
    for project in request('projects?search=' + module):
        if project['name'] != module:
            continue

        if 'namespace' not in project:
            # print("No 'namespace' in: %s - ignoring?" % project, file=sys.stderr)
            continue

        id = project['id']
        if project['namespace']['path'] in useful_namespaces:
            if project['namespace']['path'] == user_namespace:
                # If we have a branch with same name, use it.
                branch = get_project_branch(id, branchname)
                if branch is not None:
                    name = project['namespace']['path']
                    print(f"{name}/{branchname}")

                    return 'user', branch['commit']['id']
            else:
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

if __name__ == "__main__":
    user_namespace: str = os.environ['CI_PROJECT_NAMESPACE']
    project_name: str = os.environ['CI_PROJECT_NAME']
    branchname: str = os.environ['CI_COMMIT_REF_NAME']

    useful_namespaces: List[str] = ['gstreamer']
    if branchname != 'master':
        useful_namespaces.append(user_namespace)

    # Shouldn't be needed.
    remote: str = "git://anongit.freedesktop.org/gstreamer/"
    projects: str = ''
    project_template: str = "  <project name=\"{}\" remote=\"{}\" revision=\"{}\" />\n"
    user_remote: str = os.path.dirname(os.environ['CI_PROJECT_URL'])
    if not user_remote.endswith('/'):
        user_remote += '/'

    for module in GSTREAMER_MODULES:
        print(f"Checking {module}:", end=' ')

        remote = 'origin'
        revision = None
        if module == project_name:
            revision = os.environ['CI_COMMIT_SHA']
            remote = 'user'
            print(f"{user_namespace}/{branchname}")
        else:
            remote, revision = find_repository_sha(module, branchname)

        if not revision:
            revision = 'master'
        projects += project_template.format(module, remote, revision)

    with open('manifest.xml', mode='w') as manifest:
        print(MANIFEST_TEMPLATE.format(user_remote, projects), file=manifest)
