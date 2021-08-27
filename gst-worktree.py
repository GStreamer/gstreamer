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
    if not branch:
        return False
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
    # Default to the wrapper filename if 'directory' field is missing
    directory = section.get('directory', os.path.splitext(os.path.basename(wrapf))[0])
    return directory, section['revision']

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

def checkout_worktree(repo_name, repo_dir, worktree_dir, branch, new_branch, force=False):
    print('Checking out worktree for project {!r} into {!r} '
          '(branch {})'.format(repo_name, worktree_dir, branch))
    try:
        args = ["worktree", "add"]
        if force:
            args += ["-f", "-f"]
        args += [worktree_dir, branch]
        if new_branch:
            args += ["-b", new_branch]
        git(*args, repository_path=repo_dir)
    except subprocess.CalledProcessError as e:
        out = getattr(e, "output", b"").decode()
        print("\nCould not checkout worktree %s, please fix and try again."
              " Error:\n\n%s %s" % (repo_dir, out, e))

        return False

    commit_message = git("show", "--format=medium", "--shortstat", repository_path=repo_dir).split("\n")
    print(u"  -> %s%s%s - %s" % (Colors.HEADER, repo_dir, Colors.ENDC,
                                    commit_message[4].strip()))
    return True

def checkout_subprojects(worktree_dir, branch, new_branch):
    worktree_subdir = os.path.join(worktree_dir, "subprojects")

    for repo_name, repo_branch, parent_repo_dir in get_wrap_subprojects(worktree_dir, branch):
        workdir = os.path.normpath(os.path.join(worktree_subdir, repo_name))
        if not checkout_worktree(repo_name, parent_repo_dir, workdir, repo_branch, new_branch, force=True):
            return False

    return True

def remove_worktree(worktree_dir):
    worktree_subdir = os.path.join(worktree_dir, "subprojects")

    for repo_name, _, parent_repo_dir in get_wrap_subprojects(worktree_dir, None):
        workdir = os.path.normpath(os.path.join(worktree_subdir, repo_name))
        if not os.path.exists(workdir):
            continue

        subprojdir = os.path.normpath(os.path.join(SUBPROJECTS_DIR, repo_name))
        if not os.path.exists(subprojdir):
            continue

        print('Removing worktree {!r}'.format(workdir))
        try:
            git('worktree', 'remove', '-f', workdir, repository_path=subprojdir)
        except subprocess.CalledProcessError as e:
            out = getattr(e, "output", b"").decode()
            print('Ignoring error while removing worktree {!r}:\n\n{}'.format(workdir, out))

    try:
        git('worktree', 'remove', '-f', worktree_dir, repository_path=SCRIPTDIR)
    except subprocess.CalledProcessError:
        print('Failed to remove worktree {!r}'.format(worktree_dir))
        return False
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog="gst-worktree")
    parser.add_argument("--no-color", default=False, action='store_true',
                        help="Do not output ANSI colors")

    subparsers = parser.add_subparsers(help='The sub-command to run', dest='command')

    parser_add = subparsers.add_parser('add',
                                       help='Create a worktree for gst-build and all subprojects')
    parser_add.add_argument('worktree_dir', type=str,
                            help='Directory where to create the new worktree')
    parser_add.add_argument('branch', type=str, default=None,
                            help='Branch to checkout')
    parser_add.add_argument('-b', '--new-branch', type=str, default=None,
                            help='Branch to create')

    parser_rm = subparsers.add_parser('rm',
                                      help='Remove a gst-build worktree and the subproject worktrees inside it')
    parser_rm.add_argument('worktree_dir', type=str,
                           help='Worktree directory to remove')

    options = parser.parse_args()

    if options.no_color or not Colors.can_enable():
        Colors.disable()

    if not options.command:
        parser.print_usage()
        exit(1)

    worktree_dir = os.path.abspath(options.worktree_dir)

    if options.command == 'add':
        if not checkout_worktree('gst-build', SCRIPTDIR, worktree_dir, options.branch, options.new_branch):
            exit(1)
        if not checkout_subprojects(worktree_dir, options.branch, options.new_branch):
            exit(1)
    elif options.command == 'rm':
        if not os.path.exists(worktree_dir):
            print('Cannot remove worktree directory {!r}, it does not exist'.format(worktree_dir))
            exit(1)
        if not remove_worktree(worktree_dir):
            exit(1)
    else:
        # Unreachable code
        raise AssertionError
