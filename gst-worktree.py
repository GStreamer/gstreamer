#!/usr/bin/env python3

import os
import glob
import argparse
import subprocess
import configparser

from scripts.common import git
from scripts.common import Colors


SCRIPTDIR = os.path.normpath(os.path.dirname(__file__))
SUBPROJECTS_DIR = os.path.normpath(os.path.join(SCRIPTDIR, "subprojects"))

def repo_has_branch(repo_dir, branch):
    try:
        git("describe", branch, repository_path=repo_dir)
    except subprocess.CalledProcessError:
        return False
    return True

def parse_wrapfile(wrapf):
    cgp = configparser.ConfigParser()
    cgp.read(wrapf)
    if 'wrap-git' not in cgp:
        return None
    section = cgp['wrap-git']
    return section['directory'], section['revision']

def get_wrap_subprojects(srcdir, gst_branch):
    '''
    Parses wrap files in the subprojects directory for the specified source
    tree and gets the revisions for all common repos.
    '''
    for wrapf in glob.glob(os.path.join(srcdir, 'subprojects', '*.wrap')):
        entries = parse_wrapfile(wrapf)
        if not entries:
            continue

        repo_name, repo_branch = entries
        parent_repo_dir = os.path.join(SUBPROJECTS_DIR, repo_name)
        if not os.path.exists(os.path.join(parent_repo_dir, '.git')):
            continue
        # If a branch of the same name exists in the gst subproject, use it
        if repo_name.startswith('gst') and repo_has_branch(parent_repo_dir, gst_branch):
            repo_branch = gst_branch

        yield repo_name, repo_branch, parent_repo_dir

def checkout_worktree(repo_name, repo_dir, worktree_dir, branch, force=False):
    print('Checking out worktree for project {!r} into {!r} '
          '(branch {})'.format(repo_name, worktree_dir, branch))
    try:
        args = ["worktree", "add"]
        if force:
            args += ["-f", "-f"]
        args += [worktree_dir, branch]
        git(*args, repository_path=repo_dir)
    except Exception as e:
        out = getattr(e, "output", b"").decode()
        print("\nCould not checkout worktree %s, please fix and try again."
              " Error:\n\n%s %s" % (repo_dir, out, e))

        return False

    commit_message = git("show", "--shortstat", repository_path=repo_dir).split("\n")
    print(u"  -> %s%s%s - %s" % (Colors.HEADER, repo_dir, Colors.ENDC,
                                    commit_message[4].strip()))
    return True

def checkout_subprojects(worktree_dir, branch):
    worktree_subdir = os.path.join(worktree_dir, "subprojects")

    for repo_name, repo_branch, parent_repo_dir in get_wrap_subprojects(worktree_dir, branch):
        workdir = os.path.normpath(os.path.join(worktree_subdir, repo_name))
        if not checkout_worktree(repo_name, parent_repo_dir, workdir, repo_branch, force=True):
            return False

    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog="gst-worktree")


    parser.add_argument('worktree_dir', metavar='worktree_dir', type=str,
                        help='The directory where to checkout the new worktree')
    parser.add_argument('branch', metavar='branch', type=str,
                        help='The branch to checkout')
    parser.add_argument("--no-color", default=False, action='store_true',
                        help="Do not output ansi colors.")
    options = parser.parse_args()

    if options.no_color or not Colors.can_enable():
        Colors.disable()

    options.worktree_dir = os.path.abspath(options.worktree_dir)
    if not checkout_worktree('gst-build', SCRIPTDIR, options.worktree_dir, options.branch):
        exit(1)
    if not checkout_subprojects(options.worktree_dir, options.branch):
        exit(1)
