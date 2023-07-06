#!/usr/bin/env python3

from urllib.parse import urlparse
from contextlib import contextmanager
import os
import re
import sys
try:
    import gitlab
except ModuleNotFoundError:
    print("========================================================================", file=sys.stderr)
    print("ERROR: Install python-gitlab with `python3 -m pip install python-gitlab python-dateutil`", file=sys.stderr)
    print("========================================================================", file=sys.stderr)
    sys.exit(1)

try:
    from dateutil import parser as dateparse
except ModuleNotFoundError:
    print("========================================================================", file=sys.stderr)
    print("ERROR: Install dateutil with `python3 -m pip install dateutil`", file=sys.stderr)
    print("========================================================================", file=sys.stderr)
    sys.exit(1)
import argparse
import requests

import subprocess

ROOT_DIR = os.path.realpath(os.path.join(os.path.dirname(__file__), ".."))

URL = "https://gitlab.freedesktop.org/"
SIGN_IN_URL = URL + 'sign_in'
LOGIN_URL = URL + 'users/sign_in'
LOGIN_URL_LDAP = URL + '/users/auth/ldapmain/callback'

MONOREPO_REMOTE_NAME = 'origin'
NAMESPACE = "gstreamer"
MONOREPO_NAME = 'gstreamer'
MONOREPO_REMOTE = URL + f'{NAMESPACE}/{MONOREPO_NAME}'
MONOREPO_BRANCH = 'main'
PING_SIGN = '@'
MOVING_NAMESPACE = NAMESPACE

PARSER = argparse.ArgumentParser(
    description="Move merge request from old GStreamer module to the new"
                "GStreamer 'monorepo'.\n"
                " All your pending merge requests from all GStreamer modules will"
                " be moved the the mono repository."
)
PARSER.add_argument("--skip-branch", action="store", nargs="*",
                    help="Ignore MRs for branches which match those names.", dest="skipped_branches")
PARSER.add_argument("--skip-on-failure", action="store_true", default=False)
PARSER.add_argument("--dry-run", "-n", action="store_true", default=False)
PARSER.add_argument("--use-branch-if-exists",
                    action="store_true", default=False)
PARSER.add_argument("--list-mrs-only", action="store_true", default=False)
PARSER.add_argument(
    "-c",
    "--config-file",
    action="append",
    dest='config_files',
    help="Configuration file to use. Can be used multiple times.",
    required=False,
)
PARSER.add_argument(
    "-g",
    "--gitlab",
    help=(
        "Which configuration section should "
        "be used. If not defined, the default selection "
        "will be used."
    ),
    required=False,
)
PARSER.add_argument(
    "-m",
    "--module",
    help="GStreamer module to move MRs for. All if none specified. Can be used multiple times.",
    dest='modules',
    action="append",
    required=False,
)
PARSER.add_argument(
    "-mr",
    "--mr-url",
    default=None,
    type=str,
    help=(
        "URL of the MR to work on."
    ),
    required=False,
)

GST_PROJECTS = [
    'gstreamer',
    'gst-plugins-base',
    'gst-plugins-good',
    'gst-plugins-bad',
    'gst-plugins-ugly',
    'gst-libav',
    'gst-rtsp-server',
    'gstreamer-vaapi',
    'gstreamer-sharp',
    'gst-python',
    'gst-editing-services',
    'gst-devtools',
    'gst-docs',
    'gst-examples',
    'gst-build',
    'gst-ci',
]

GST_PROJECTS_ID = {
    'gstreamer': 1357,
    'gst-rtsp-server': 1362,
    'gstreamer-vaapi': 1359,
    'gstreamer-sharp': 1358,
    'gst-python': 1355,
    'gst-plugins-ugly': 1354,
    'gst-plugins-good': 1353,
    'gst-plugins-base': 1352,
    'gst-plugins-bad': 1351,
    'gst-libav': 1349,
    'gst-integration-testsuites': 1348,
    'gst-examples': 1347,
    'gst-editing-services': 1346,
    'gst-docs': 1345,
    'gst-devtools': 1344,
    'gst-ci': 1343,
    'gst-build': 1342,
}

# We do not want to deal with LFS
os.environ["GIT_LFS_SKIP_SMUDGE"] = "1"


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


class GstMRMover:
    def __init__(self):

        self.modules = []
        self.gitlab = None
        self.config_files = []
        self.gl = None
        self.mr = None
        self.mr_url = None
        self.all_projects = []
        self.skipped_branches = []
        self.git_rename_limit = None
        self.skip_on_failure = None
        self.dry_run = False

    def connect(self):
        fprint("Logging into gitlab...")

        if self.gitlab:
            gl = gitlab.Gitlab.from_config(self.gitlab, self.config_files)
            fprint(f"{green(' OK')}\n", nested=False)
            return gl

        gitlab_api_token = os.environ.get('GITLAB_API_TOKEN')
        if gitlab_api_token:
            gl = gitlab.Gitlab(URL, private_token=gitlab_api_token)
            fprint(f"{green(' OK')}\n", nested=False)
            return gl

        session = requests.Session()
        sign_in_page = session.get(SIGN_IN_URL).content.decode()
        for line in sign_in_page.split('\n'):
            m = re.search('name="authenticity_token" value="([^"]+)"', line)
            if m:
                break

        token = None
        if m:
            token = m.group(1)

        if not token:
            fprint(f"{red('Unable to find the authenticity token')}\n")
            sys.exit(1)

        for data, url in [
            ({'user[login]': 'login_or_email',
              'user[password]': 'SECRET',
              'authenticity_token': token}, LOGIN_URL),
            ({'username': 'login_or_email',
              'password': 'SECRET',
              'authenticity_token': token}, LOGIN_URL_LDAP)]:

            r = session.post(url, data=data)
            if r.status_code != 200:
                continue

            try:
                gl = gitlab.Gitlab(URL, api_version=4, session=session)
                gl.auth()
            except gitlab.exceptions.GitlabAuthenticationError as e:
                continue
            return gl

        sys.exit(bold(f"{red('FAILED')}.\n\nPlease go to:\n\n"
                      '   https://gitlab.freedesktop.org/-/profile/personal_access_tokens\n\n'
                      f'and generate a token {bold("with read/write access to all but the registry")},'
                      ' then set it in the "GITLAB_API_TOKEN" environment variable:"'
                      f'\n\n  $ GITLAB_API_TOKEN=<your token> {" ".join(sys.argv)}\n'))

    def git(self, *args, can_fail=False, interaction_message=None, call=False, revert_operation=None):
        cwd = ROOT_DIR
        retry = True
        while retry:
            retry = False
            try:
                if not call:
                    try:
                        return subprocess.check_output(["git"] + list(args), cwd=cwd,
                                                       stdin=subprocess.DEVNULL,
                                                       stderr=subprocess.STDOUT).decode()
                    except subprocess.CalledProcessError:
                        if not can_fail:
                            fprint(
                                f"\n\n{bold(red('ERROR'))}: `git {' '.join(args)}` failed" + "\n", nested=False)
                        raise
                else:
                    subprocess.call(["git"] + list(args), cwd=cwd)
                    return "All good"
            except Exception as e:
                if interaction_message:
                    if self.skip_on_failure:
                        return "SKIP"
                    output = getattr(e, "output", b"")
                    if output is not None:
                        out = output.decode()
                    else:
                        out = "????"
                    fprint(f"\n```"
                           f"\n{out}\n"
                           f"Entering a shell in {cwd} to fix:\n\n"
                           f" {bold(interaction_message)}\n\n"
                           f"You should then exit with the following codes:\n\n"
                           f"  - {bold('`exit 0`')}: once you have fixed the problem and we can keep moving the merge request\n"
                           f"  - {bold('`exit 1`')}: {bold('retry')}: once you have let the repo in a state where the operation should be to retried\n"
                           f"  - {bold('`exit 2`')}: to skip that merge request\n"
                           f"  - {bold('`exit 3`')}: stop the script and abandon moving your MRs\n"
                           "\n```\n", nested=False)
                    try:
                        if os.name == 'nt':
                            shell = os.environ.get(
                                "COMSPEC", r"C:\WINDOWS\system32\cmd.exe")
                        else:
                            shell = os.environ.get(
                                "SHELL", os.path.realpath("/bin/sh"))
                        subprocess.check_call(shell, cwd=cwd)
                    except subprocess.CalledProcessError as e:
                        if e.returncode == 1:
                            retry = True
                            continue
                        elif e.returncode == 2:
                            if revert_operation:
                                self.git(*revert_operation, can_fail=True)
                            return "SKIP"
                        elif e.returncode == 3:
                            if revert_operation:
                                self.git(*revert_operation, can_fail=True)
                            sys.exit(3)
                    except Exception:
                        # Result of subshell does not really matter
                        pass

                    return "User fixed it"

                if can_fail:
                    return "Failed but we do not care"

                raise e

    def cleanup_args(self):
        if self.mr_url:
            self.modules.append(GST_PROJECTS[0])
            (namespace, module, _, _, mr) = os.path.normpath(urlparse(self.mr_url).path).split('/')[1:]
            self.modules.append(module)
            self.mr = int(mr)
        elif not self.modules:
            if self.mr:
                sys.exit(f"{red(f'Merge request #{self.mr} specified without module')}\n\n"
                         f"{bold(' -> Use `--module` to specify which module the MR is from.')}")

            self.modules = GST_PROJECTS
        else:
            VALID_PROJECTS = GST_PROJECTS[1:]
            for m in self.modules:
                if m not in VALID_PROJECTS:
                    projects = '\n- '.join(VALID_PROJECTS)
                    sys.exit(
                        f"{red(f'Unknown module {m}')}\nModules are:\n- {projects}")
            if self.mr and len(self.modules) > 1:
                sys.exit(f"{red(f'Merge request #{self.mr} specified but several modules where specified')}\n\n"
                         f"{bold(' -> Use `--module` only once to specify an merge request.')}")
            self.modules.append(GST_PROJECTS[0])

    def run(self):
        self.cleanup_args()
        self.gl = self.connect()
        self.gl.auth()

        # Skip pre-commit hooks when migrating. Some users may have a
        # different version of gnu indent and that can lead to cherry-pick
        # failing.
        os.environ["GST_DISABLE_PRE_COMMIT_HOOKS"] = "1"

        try:
            prevbranch = self.git(
                "rev-parse", "--abbrev-ref", "HEAD", can_fail=True).strip()
        except Exception:
            fprint(bold(yellow("Not on a branch?\n")), indent=False)
            prevbranch = None

        try:
            self.setup_repo()

            from_projects, to_project = self.fetch_projects()

            with nested('  '):
                self.move_mrs(from_projects, to_project)
        finally:
            if self.git_rename_limit is not None:
                self.git("config", "merge.renameLimit",
                         str(self.git_rename_limit))
            if prevbranch:
                fprint(f'Back to {prevbranch}\n')
                self.git("checkout", prevbranch)

    def fetch_projects(self):
        fprint("Fetching projects... ")
        self.all_projects = [proj for proj in self.gl.projects.list(
            membership=1, all=True) if proj.name in self.modules]

        try:
            self.user_project, = [p for p in self.all_projects
                                  if p.namespace['path'] == self.gl.user.username
                                  and p.name == MONOREPO_NAME]
        except ValueError:
            fprint(
                f"{red(f'ERROR')}\n\nCould not find repository {self.gl.user.name}/{MONOREPO_NAME}")
            fprint(f"{red(f'Got to https://gitlab.freedesktop.org/gstreamer/gstreamer/ and create a fork so we can move your Merge requests.')}")
            sys.exit(1)
        fprint(f"{green(' OK')}\n", nested=False)

        from_projects = []
        user_projects_name = [proj.name for proj in self.all_projects if proj.namespace['path']
                              == self.gl.user.username and proj.name in GST_PROJECTS]
        for project, id in GST_PROJECTS_ID.items():
            if project not in user_projects_name or project == 'gstreamer':
                continue

            projects = [p for p in self.all_projects if p.id == id]
            if not projects:
                upstream_project = self.gl.projects.get(id)
            else:
                upstream_project, = projects
            assert project

            from_projects.append(upstream_project)

        fprint(f"\nMoving MRs from:\n")
        fprint(f"----------------\n")
        for p in from_projects:
            fprint(f"  - {bold(p.path_with_namespace)}\n")

        to_project = self.gl.projects.get(GST_PROJECTS_ID['gstreamer'])
        fprint(f"To: {bold(to_project.path_with_namespace)}\n\n")

        return from_projects, to_project

    def recreate_mr(self, project, to_project, mr):
        branch = f"{project.name}-{mr.source_branch}"
        if not self.create_branch_for_mr(branch, project, mr):
            return None

        description = f"**Copied from {URL}/{project.path_with_namespace}/-/merge_requests/{mr.iid}**\n\n{mr.description}"

        title = mr.title
        if ':' not in mr.title:
            title = f"{project.name}: {mr.title}"

        new_mr_dict = {
            'source_branch': branch,
            'allow_collaboration': True,
            'remove_source_branch': True,
            'target_project_id': to_project.id,
            'target_branch': MONOREPO_BRANCH,
            'title': title,
            'labels': mr.labels,
            'description': description,
        }

        try:
            fprint(f"-> Recreating MR '{bold(mr.title)}'...")
            if self.dry_run:
                fprint(f"\nDry info:\n{new_mr_dict}\n")
            else:
                new_mr = self.user_project.mergerequests.create(new_mr_dict)
                fprint(f"{green(' OK')}\n", nested=False)
        except gitlab.exceptions.GitlabCreateError as e:
            fprint(f"{yellow('SKIPPED')} (An MR already exists)\n", nested=False)
            return None

        fprint(f"-> Adding discussings from MR '{mr.title}'...")
        if self.dry_run:
            fprint(f"{green(' OK')}\n", nested=False)
            return None

        new_mr_url = f"{URL}/{to_project.path_with_namespace}/-/merge_requests/{new_mr.iid}"
        for issue in mr.closes_issues():
            obj = {'body': f'Fixing MR moved to: {new_mr_url}'}
            issue.discussions.create(obj)

        mr_url = f"{URL}/{project.path_with_namespace}/-/merge_requests/{mr.iid}"
        for discussion in mr.discussions.list():
            # FIXME notes = [n for n in discussion.attributes['notes'] if n['type'] is not None]
            notes = [n for n in discussion.attributes['notes']]
            if not notes:
                continue

            new_discussion = None
            for note in notes:
                note = discussion.notes.get(note['id'])

                note_url = f"{mr_url}#note_{note.id}"
                when = dateparse.parse(
                    note.created_at).strftime('on %d, %b %Y')
                body = f"**{note.author['name']} - {PING_SIGN}{note.author['username']} wrote [here]({note_url})** {when}:\n\n"
                body += '\n'.join([line for line in note.body.split('\n')])

                obj = {
                    'body': body,
                    'type': note.type,
                    'resolvable': note.resolvable,
                }

                if new_discussion:
                    new_discussion.notes.create(obj)
                else:
                    new_discussion = new_mr.discussions.create(obj)

                if not note.resolvable or note.resolved:
                    new_discussion.resolved = True
                    new_discussion.save()

        fprint(f"{green(' OK')}\n", nested=False)

        print(f"New MR available at: {bold(new_mr_url)}\n")

        return new_mr

    def push_branch(self, branch):
        fprint(
            f"-> Pushing branch {branch} to remote {self.gl.user.username}...")
        if self.git("push", "--no-verify", self.gl.user.username, branch,
                    interaction_message=f"pushing {branch} to {self.gl.user.username} with:\n  "
                    f" `$git push {self.gl.user.username} {branch}`") == "SKIP":
            fprint(yellow("'SKIPPED' (couldn't push)"), nested=False)

            return False

        fprint(f"{green(' OK')}\n", nested=False)

        return True

    def create_branch_for_mr(self, branch, project, mr):
        remote_name = project.name + '-' + self.gl.user.username
        remote_branch = f"{MONOREPO_REMOTE_NAME}/{MONOREPO_BRANCH}"
        if self.use_branch_if_exists:
            try:
                self.git("checkout", branch)
                self.git("show", remote_branch + "..", call=True)
                if self.dry_run:
                    fprint("Dry run... not creating MR")
                    return True
                cont = input('\n     Create MR [y/n]? ')
                if cont.strip().lower() != 'y':
                    fprint("Cancelled")
                    return False
                return self.push_branch(branch)
            except subprocess.CalledProcessError as e:
                pass

        self.git("remote", "add", remote_name,
                 f"{URL}{self.gl.user.username}/{project.name}.git", can_fail=True)
        self.git("fetch", remote_name)

        if self.git("checkout", remote_branch, "-b", branch,
                    interaction_message=f"checking out branch with `git checkout {remote_branch} -b {branch}`") == "SKIP":
            fprint(
                bold(f"{red('SKIPPED')} (couldn't checkout)\n"), nested=False)
            return False

        # unset upstream to avoid to push to main (ie push.default = tracking)
        self.git("branch", branch, "--unset-upstream")

        for commit in reversed([c for c in mr.commits()]):
            if self.git("cherry-pick", commit.id,
                        interaction_message=f"cherry-picking {commit.id} onto {branch} with:\n  "
                        f" `$ git cherry-pick {commit.id}`",
                        revert_operation=["cherry-pick", "--abort"]) == "SKIP":
                fprint(
                    f"{yellow('SKIPPED')} (couldn't cherry-pick).", nested=False)
                return False

        self.git("show", remote_branch + "..", call=True)
        if self.dry_run:
            fprint("Dry run... not creating MR\n")
            return True
        cont = input('\n     Create MR [y/n]? ')
        if cont.strip().lower() != 'y':
            fprint(f"{red('Cancelled')}\n", nested=False)
            return False

        return self.push_branch(branch)

    def move_mrs(self, from_projects, to_project):
        failed_mrs = []
        found_mr = None
        for from_project in from_projects:
            with nested(f'{bold(from_project.path_with_namespace)}'):
                fprint(f'Fetching mrs')
                mrs = [mr for mr in from_project.mergerequests.list(
                    all=True, author_id=self.gl.user.id) if mr.author['username'] == self.gl.user.username and mr.state == "opened"]
                if not mrs:
                    fprint(f"{yellow(' None')}\n", nested=False)
                    continue

                fprint(f"{green(' DONE')}\n", nested=False)

                for mr in mrs:
                    if self.mr:
                        if self.mr != mr.iid:
                            continue
                        found_mr = True
                    fprint(
                        f'Moving {mr.source_branch} "{mr.title}": {URL}{from_project.path_with_namespace}/merge_requests/{mr.iid}... ')
                    if mr.source_branch in self.skipped_branches:
                        print(f"{yellow('SKIPPED')} (blacklisted branch)")
                        failed_mrs.append(
                            f"{URL}{from_project.path_with_namespace}/merge_requests/{mr.iid}")
                        continue
                    if self.list_mrs_only:
                        fprint("\n"f"List only: {yellow('SKIPPED')}\n")
                        continue

                    with nested(f'{bold(from_project.path_with_namespace)}: {mr.iid}'):
                        new_mr = self.recreate_mr(from_project, to_project, mr)
                        if not new_mr:
                            if not self.dry_run:
                                failed_mrs.append(
                                    f"{URL}{from_project.path_with_namespace}/merge_requests/{mr.iid}")
                        else:
                            fprint(f"{green(' OK')}\n", nested=False)

                        self.close_mr(from_project, to_project, mr, new_mr)

            fprint(
                f"\n{yellow('DONE')} with {from_project.path_with_namespace}\n\n", nested=False)

        if self.mr and not found_mr:
            sys.exit(
                bold(red(f"\n==> Couldn't find MR {self.mr} in {self.modules[0]}\n")))

        for mr in failed_mrs:
            fprint(f"Didn't move MR: {mr}\n")

    def close_mr(self, project, to_project, mr, new_mr):
        if new_mr:
            new_mr_url = f"{URL}/{to_project.path_with_namespace}/-/merge_requests/{new_mr.iid}"
        else:
            new_mr_url = None
        mr_url = f"{URL}/{project.path_with_namespace}/-/merge_requests/{mr.iid}"
        cont = input(f'\n  Close old MR {mr_url} "{bold(mr.title)}" ? [y/n]')
        if cont.strip().lower() != 'y':
            fprint(f"{yellow('Not closing old MR')}\n")
        else:
            obj = None
            if new_mr_url:
                obj = {'body': f"Moved to: {new_mr_url}"}
            else:
                ret = input(
                    f"Write a comment to add while closing MR {mr.iid} '{bold(mr.title)}':\n\n").strip()
                if ret:
                    obj = {'body': ret}

            if self.dry_run:
                fprint(f"{bold('Dry run, not closing')}\n", nested=False)
            else:
                if obj:
                    mr.discussions.create(obj)
                mr.state_event = 'close'
                mr.save()
                fprint(
                    f'Old MR {mr_url} "{bold(mr.title)}" {yellow("CLOSED")}\n')

    def setup_repo(self):
        fprint(f"Setting up '{bold(ROOT_DIR)}'...")

        try:
            out = self.git("status", "--porcelain")
            if out:
                fprint("\n" + red('Git repository is not clean:')
                       + "\n```\n" + out + "\n```\n")
                sys.exit(1)

        except Exception as e:
            exit(
                f"Git repository{ROOT_DIR} is not clean. Clean it up before running {sys.argv[0]}\n ({e})")

        self.git('remote', 'add', MONOREPO_REMOTE_NAME,
                 MONOREPO_REMOTE, can_fail=True)
        self.git('fetch', MONOREPO_REMOTE_NAME)

        self.git('remote', 'add', self.gl.user.username,
                 f"git@gitlab.freedesktop.org:{self.gl.user.username}/gstreamer.git", can_fail=True)
        self.git('fetch', self.gl.user.username,
                 interaction_message=f"Setup your fork of {URL}gstreamer/gstreamer as remote called {self.gl.user.username}")
        fprint(f"{green(' OK')}\n", nested=False)

        try:
            git_rename_limit = int(self.git("config", "merge.renameLimit"))
        except subprocess.CalledProcessError:
            git_rename_limit = 0
        if int(git_rename_limit) < 999999:
            self.git_rename_limit = git_rename_limit
            fprint(
                "-> Setting git rename limit to 999999 so we can properly cherry-pick between repos\n")
            self.git("config", "merge.renameLimit", "999999")


def main():
    mover = GstMRMover()
    PARSER.parse_args(namespace=mover)
    mover.run()


if __name__ == '__main__':
    main()
