#!/usr/bin/env python3

import argparse
import os
import sys
import subprocess
import urllib.error
import urllib.parse
import urllib.request
import json

from typing import Dict, Tuple, List
# from pprint import pprint

if sys.version_info < (3, 6):
    raise SystemExit('Need Python 3.6 or newer')

GSTREAMER_MODULES: List[str] = [
    'orc',
    'cerbero',
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
    'gstreamer-sharp',
    'gstreamer-vaapi',
    'gst-integration-testsuites',
    'gst-examples',
]

MANIFEST_TEMPLATE: str = """<?xml version="1.0" encoding="UTF-8"?>
<manifest>
  <remote fetch="{}" name="user"/>
  <remote fetch="https://gitlab.freedesktop.org/gstreamer/" name="origin"/>
{}
</manifest>"""


CERBERO_DEPS_LOGS_TARGETS = (
    ('cross-ios', 'universal'),
    ('cross-windows-mingw', 'x86'),
    ('cross-windows-mingw', 'x86_64'),
    ('cross-android', 'universal'),
    ('fedora', 'x86_64'),
    ('macos', 'x86_64'),
    ('windows-msvc', 'x86_64'),
)

# Disallow git prompting for a username/password
os.environ['GIT_TERMINAL_PROMPT'] = '0'
def git(*args, repository_path='.'):
    return subprocess.check_output(["git"] + list(args), cwd=repository_path).decode()

def get_cerbero_last_build_info (branch : str):
    # Fetch the deps log for all (distro, arch) targets
    all_commits = {}
    for distro, arch in CERBERO_DEPS_LOGS_TARGETS:
        url = f'https://artifacts.gstreamer-foundation.net/cerbero-deps/{branch}/{distro}/{arch}/cerbero-deps.log'
        print(f'Fetching {url}')
        try:
            req = urllib.request.Request(url)
            resp = urllib.request.urlopen(req);
            deps = json.loads(resp.read())
        except urllib.error.URLError as e:
            print(f'WARNING: Failed to GET {url}: {e!s}')
            continue

        for dep in deps:
            commit = dep['commit']
            if commit not in all_commits:
                all_commits[commit] = []
            all_commits[commit].append((distro, arch))

    # Fetch the cerbero commit that has the most number of caches
    best_commit = None
    newest_commit = None
    max_caches = 0
    total_caches = len(CERBERO_DEPS_LOGS_TARGETS)
    for commit, targets in all_commits.items():
        if newest_commit is None:
            newest_commit = commit
        have_caches = len(targets)
        # If this commit has caches for all targets, just use it
        if have_caches == total_caches:
            best_commit = commit
            break
        # Else, try to find the commit with the most caches
        if have_caches > max_caches:
            max_caches = have_caches
            best_commit = commit
    if newest_commit is None:
        print('WARNING: No deps logs were found, will build from scratch')
    if best_commit != newest_commit:
        print(f'WARNING: Cache is not up-to-date for commit {newest_commit}, using commit {best_commit} instead')
    return best_commit


def get_branch_info(module: str, namespace: str, branch: str) -> Tuple[str, str]:
    try:
        res = git('ls-remote', f'https://gitlab.freedesktop.org/{namespace}/{module}.git', branch)
    except subprocess.CalledProcessError:
        return None, None

    if not res:
        return None, None

    # Special case cerbero to avoid cache misses
    if module == 'cerbero':
        sha = get_cerbero_last_build_info(branch)
        if sha is not None:
            return sha, sha

    lines = res.split('\n')
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
    ups_branch: str = os.getenv('GST_UPSTREAM_BRANCH', default='master')

    if module == "orc":
        ups_branch = os.getenv('ORC_UPSTREAM_BRANCH', default='master')

    if module == os.environ['CI_PROJECT_NAME']:
        return 'user', branchname, os.environ['CI_COMMIT_SHA']

    if branchname != ups_branch:
        remote_refname, sha = get_branch_info(module, namespace, branchname)
        if sha is not None:
            return 'user', remote_refname, sha

    # Check upstream project for a branch
    remote_refname, sha = get_branch_info(module, 'gstreamer', ups_branch)
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
    os.environ["GST_UPSTREAM_BRANCH"] = "master"
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

    os.environ['GST_UPSTREAM_BRANCH'] = '1.12'
    remote, refname, twelve = find_repository_sha('gst-plugins-good', '1.12')
    assert twelve is not None
    assert remote == 'origin'
    assert refname == "refs/heads/1.12"

    os.environ['GST_UPSTREAM_BRANCH'] = '1.14'
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
