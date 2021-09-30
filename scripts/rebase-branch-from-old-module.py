#!/usr/bin/env python3

from pathlib import Path as P
from urllib.parse import urlparse
from contextlib import contextmanager
import os
import re
import sys

import argparse
import requests

import subprocess

import random
import string

URL = "https://gitlab.freedesktop.org/"
PARSER = argparse.ArgumentParser(
    description="`Rebase` a branch from an old GStreamer module onto the monorepo"
)
PARSER.add_argument("repo", help="The repo with the old module to use. ie https://gitlab.freedesktop.org/user/gst-plugins-bad.git or /home/me/gst-build/subprojects/gst-plugins-bad")
PARSER.add_argument("branch", help="The branch to rebase.")

log_depth = []               # type: T.List[str]


@contextmanager
def nested(name=''):
    global log_depth
    log_depth.append(name)
    try:
        yield
    finally:
        log_depth.pop()


def bold(text: str):
    return f"\033[1m{text}\033[0m"


def green(text: str):
    return f"\033[1;32m{text}\033[0m"


def red(text: str):
    return f"\033[1;31m{text}\033[0m"


def yellow(text: str):
    return f"\033[1;33m{text}\033[0m"


def fprint(msg, nested=True):
    if log_depth:
        prepend = log_depth[-1] + ' | ' if nested else ''
    else:
        prepend = ''

    print(prepend + msg, end="")
    sys.stdout.flush()


class GstCherryPicker:
    def __init__(self):

        self.branch = None
        self.repo = None
        self.module = None

        self.git_rename_limit = None

    def check_clean(self):
        try:
            out = self.git("status", "--porcelain")
            if out:
                fprint("\n" + red('Git repository is not clean:') + "\n```\n" + out + "\n```\n")
                sys.exit(1)

        except Exception as e:
            sys.exit(
                f"Git repository is not clean. Clean it up before running ({e})")

    def run(self):
        assert self.branch
        assert self.repo
        self.check_clean()

        try:
            git_rename_limit = int(self.git("config", "merge.renameLimit"))
        except subprocess.CalledProcessError:
            git_rename_limit = 0
        if int(git_rename_limit) < 999999:
            self.git_rename_limit = git_rename_limit
            fprint("-> Setting git rename limit to 999999 so we can properly cherry-pick between repos")
            self.git("config", "merge.renameLimit", "999999")
            fprint(f"{green(' OK')}\n", nested=False)

        try:
            self.rebase()
        finally:
            if self.git_rename_limit is not None:
                self.git("config", "merge.renameLimit", str(self.git_rename_limit))

    def rebase(self):
        repo = urlparse(self.repo)

        repo_path = P(repo.path)
        self.module = module = repo_path.stem
        remote_name = f"{module}-{repo_path.parent.name}"
        fprint('Adding remotes...')
        self.git("remote", "add", remote_name, self.repo, can_fail=True)
        self.git("remote", "add", module, f"{URL}gstreamer/{module}.git",
                can_fail=True)
        fprint(f"{green(' OK')}\n", nested=False)

        fprint(f'Fetching {remote_name}...')
        self.git("fetch", remote_name,
            interaction_message=f"fetching {remote_name} with:\n"
            f"   `$ git fetch {remote_name}`")
        fprint(f"{green(' OK')}\n", nested=False)

        fprint(f'Fetching {module}...')
        self.git("fetch", module,
            interaction_message=f"fetching {module} with:\n"
            f"   `$ git fetch {module}`")
        fprint(f"{green(' OK')}\n", nested=False)

        prevbranch = self.git("rev-parse", "--abbrev-ref", "HEAD").strip()
        tmpbranchname = f"{remote_name}_{self.branch}"
        fprint(f'Checking out branch {remote_name}/{self.branch} as {tmpbranchname}\n')
        try:
            self.git("checkout", f"{remote_name}/{self.branch}", "-b", tmpbranchname)
            self.git("rebase", f"{module}/master",
                interaction_message=f"Failed rebasing {remote_name}/{self.branch} on {module}/master with:\n"
                f"   `$ git rebase {module}/master`")
            ret = self.cherry_pick(tmpbranchname)
        except Exception as e:
            self.git("rebase", "--abort", can_fail=True)
            self.git("checkout", prevbranch)
            self.git("branch", "-D", tmpbranchname)
            raise
        if ret:
            fprint(f"{green(' OK')}\n", nested=False)
        else:
            self.git("checkout", prevbranch)
            self.git("branch", "-D", tmpbranchname)
            fprint(f"{red(' ERROR')}\n", nested=False)

    def cherry_pick(self, branch):
        shas = self.git('log', '--format=format:%H', f'{self.module}/master..').strip()
        fprint(f'Resetting on origin/main')
        self.git("reset", "--hard", "origin/main")
        fprint(f"{green(' OK')}\n", nested=False)

        for sha in reversed(shas.split()):
            fprint(f' - Cherry picking: {bold(sha)}\n')
            try:
                self.git("cherry-pick", sha,
                        interaction_message=f"cherry-picking {sha} onto {branch} with:\n  "
                        f" `$ git cherry-pick {sha}`",
                        revert_operation=["cherry-pick", "--abort"])
            except Exception as e:
                fprint(f' - Cherry picking failed: {bold(sha)}\n')
                return False
        return True

    def git(self, *args, can_fail=False, interaction_message=None, call=False, revert_operation=None):
        retry = True
        while retry:
            retry = False
            try:
                if not call:
                    try:
                        return subprocess.check_output(["git"] + list(args),
                                                    stdin=subprocess.DEVNULL,
                                                    stderr=subprocess.STDOUT).decode()
                    except Exception as e:
                        if not can_fail:
                            fprint(f"\n\n{bold(red('ERROR'))}: `git {' '.join(args)}` failed" + "\n", nested=False)
                        raise
                else:
                    subprocess.call(["git"] + list(args))
                    return "All good"
            except Exception as e:
                if interaction_message:
                    output = getattr(e, "output", b"")
                    if output is not None:
                        out = output.decode()
                    else:
                        out = "????"
                    fprint(f"\n```"
                          f"\n{out}\n"
                          f"Entering a shell to fix:\n\n"
                          f" {bold(interaction_message)}\n\n"
                          f"You should then exit with the following codes:\n\n"
                          f"  - {bold('`exit 0`')}: once you have fixed the problem and we can keep moving the \n"
                          f"  - {bold('`exit 1`')}: {bold('retry')}: once you have let the repo in a state where cherry-picking the commit should be to retried\n"
                          f"  - {bold('`exit 2`')}: stop the script and abandon rebasing your branch\n"
                          "\n```\n", nested=False)
                    try:
                        if os.name == 'nt':
                            shell = os.environ.get(
                                "COMSPEC", r"C:\WINDOWS\system32\cmd.exe")
                        else:
                            shell = os.environ.get(
                                "SHELL", os.path.realpath("/bin/sh"))
                        subprocess.check_call(shell)
                    except subprocess.CalledProcessError as e:
                        if e.returncode == 1:
                            retry = True
                            continue
                        elif e.returncode == 2:
                            if revert_operation:
                                self.git(*revert_operation, can_fail=True)
                        raise
                    except Exception as e:
                        # Result of subshell does not really matter
                        pass

                    return "User fixed it"

                if can_fail:
                    return "Failed but we do not care"

                raise e


def main():
    picker = GstCherryPicker()
    PARSER.parse_args(namespace=picker)
    picker.run()


if __name__ == '__main__':
    main()
