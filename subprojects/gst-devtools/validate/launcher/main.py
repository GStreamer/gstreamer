#!/usr/bin/env python3
#
# Copyright (c) 2014,Thibault Saunier <thibault.saunier@collabora.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.
import os
import sys
import shutil
from . import utils
import urllib.parse
from . import loggable
import multiprocessing
import argparse
import tempfile
from . import reporters
import subprocess
import pathlib


from .loggable import Loggable
from .baseclasses import _TestsLauncher, ScenarioManager
from .utils import printc, path2url, DEFAULT_MAIN_DIR, launch_command, Colors, Protocols, which


LESS = "less"
HELP = '''

===============================================================================
                       gst-validate-launcher
===============================================================================

1. Introduction
----------------

gst-validate-launcher is a test launcher tool. It has been designed to
launch the various tools included in GstValidate, running tests on real
media files. This means that with gst-validate-launcher, you can launch
many tests automatically in one simple command. It then permits to
aggregate results and print them in a human readable way on stdout
and serializing them in the following implemented formats:

 * %s

We support all the tools provided in GstValidate in the launcher, but
we also support ges-launch when the GStreamer Editing Services have
been compiled against GstValidate.

2. Default test suite
---------------------

A default suite of tests is provided and is available at: http://gitlab.freedesktop.org/gstreamer/gst-integration-testsuites/
You can run it pretty simply doing:

.    $gst-validate-launcher --sync

That will download Gstreamer upstream default assets into the
default folder (%s) and run all currently
activated tests. Note that we use git-annex https://git-annex.branchable.com/ so
you will need that tool to get started.

3. Implement your own tests
---------------------------

To implement new tests, you will just need to set the media path using the
--medias-paths argument. If you want to run all available scenarios on all the
file present in that folder, you should run the first time:

.    $gst-validate-launcher --medias-paths /path/to/media/files --generate-media-info

That will generate the .media_info files that contains information about the media
files present in that folder. Those media_info files are simple XML file describing
the topology of the media files. You need not reuse --generate-media-info from
next time. The generated media files will be used as a reference for following
runs. You might want to check that they contain the right information yourself
the first time.

Once .media-info is generated, you can update it using --update-media-info.

Those .media_info are the files that are used by gst-validate-launcher to know
what media files can be used for the different scenarios. For example if a
file is not seekable, seeking scenarios will not be run on it etc...

3.1 Scenarios specific to a media file/stream:
----------------------------------------------

It is possible that some scenarios are very specific to one media file. In that case,
the .scenario file should be present in the same folder as the .media_info file and
be called similarly. For example for a file called /some/media/file.mp4, the media_info
file will be called /some/media/file.media_info and a scenario that will seek to a position that
is known to fail would be called: /some/media/file.mp4.seek_to_failing_pos.scenario and
gst-validate-launcher will run that scenario only on that media file.

3.2 Test media accessible through other protocols:
--------------------------------------------------

Currently gst-validate-launcher supports the following protocols:

  * %s

It does not mean you can not test other protocols but it means that it has not been
properly tested.

To test medias that use those protocols, you should simply make sure that there
is a media descriptor file with .stream_info as an extension in your --media-paths.
You can generate such a file doing:

.   $gst-validate-media-check-1.0 http://devimages.apple.com/iphone/samples/bipbop/bipbopall.m3u8 --output-file /somewhere/in/you/media/path/bipbop.stream_info

Once this is done, gst-validate-launcher will run the scenarios on those media files the
same way as if they were local files.


4. Debug gst-validate-launcher execution
----------------------------------------

You can activate debug logs setting the environment variable GST_VALIDATE_LAUNCHER_DEBUG.

.   $GST_VALIDATE_LAUNCHER_DEBUG=6 gst-validate-launcher

It uses the same syntax as PITIVI_DEBUG (more information at:
https://developer.pitivi.org/Bug_reporting.html#debug-logs).
''' % ("\n  * ".join([reporter.name for reporter in
                      utils.get_subclasses(reporters.Reporter, reporters.__dict__)]
                     ),
       DEFAULT_MAIN_DIR,
       "\n  * ".join([getattr(Protocols, att) for att in
                      dir(Protocols) if isinstance(getattr(Protocols, att), str) and not
                      att.startswith("_")]))

if "--help" not in sys.argv:
    HELP = "Use --help for the full help"

QA_ASSETS = "gstreamer"
MEDIAS_FOLDER = "medias"
DEFAULT_GST_QA_ASSETS_REPO = "https://gitlab.freedesktop.org/gstreamer/gstreamer.git"


def download_assets(options):
    try:
        printc("About to download assets from %s to %s" % (options.remote_assets_url,
               options.clone_dir))
        launch_command("%s %s %s" % (options.get_assets_command,
                                     options.remote_assets_url,
                                     options.clone_dir),
                       fails=True)
    except subprocess.CalledProcessError as e:
        if "git" in options.get_assets_command:
            m = "\n\nMAKE SURE YOU HAVE git INSTALLED!"
        else:
            m = ""

        printc("Could not download assets\n\nError: %s%s" % (e, m),
               Colors.FAIL, True)

        return False

    return True


class PrintUsage(argparse.Action):

    def __init__(self, option_strings, dest=argparse.SUPPRESS, default=argparse.SUPPRESS, help=None):
        super(
            PrintUsage, self).__init__(option_strings=option_strings, dest=dest,
                                       default=default, nargs=0, help=help)

    def __call__(self, parser, namespace, values, option_string=None):
        print(HELP)
        parser.exit()


def _positive_integer_type(value):
    cast = int(value)
    if cast <= 0:
        raise argparse.ArgumentTypeError(f'`{value}\' is not a positive integer')
    return cast


class LauncherConfig(Loggable):

    def __init__(self):
        self.testsuites = []
        self.debug = False
        self.forever = False
        self.n_runs = None
        self.fatal_error = False
        self.wanted_tests = []
        self.blacklisted_tests = []
        self.list_tests = False
        self.mute = True
        self.unmute = not self.mute
        self.no_color = False
        self.generate_info = False
        self.update_media_info = False
        self.generate_info_full = False
        self.long_limit = utils.LONG_TEST
        self.config = None
        self.valgrind = False
        self.gdb = False
        self.no_display = False
        self.rr = False
        self.xunit_file = None
        self.main_dir = utils.DEFAULT_MAIN_DIR
        self.output_dir = None
        self.logsdir = None
        self.privatedir = None
        self.redirect_logs = False
        self.num_jobs = max(multiprocessing.cpu_count(), 1)
        self.dest = None
        self._using_default_paths = False
        self.keep_logs = False
        # paths passed with --media-path, and not defined by a testsuite
        self.user_paths = []
        self.paths = []
        self.testsuites_dirs = utils.DEFAULT_TESTSUITES_DIRS

        self.clone_dir = None

        self.http_server_port = 8079
        self.http_bandwith = 1024 * 1024
        self.http_server_dir = None
        self.httponly = False
        self.get_assets_command = "git clone"
        self.remote_assets_url = DEFAULT_GST_QA_ASSETS_REPO
        self.sync = False
        self.force_sync = False
        self.sync_all = False
        self.sync_version = None
        self.check_bugs_status = False
        self.retry_on_failures = False
        self.html = False

    def cleanup(self):
        """
        Cleanup the options looking after user options have been parsed
        """

        # Get absolute path for main_dir and base everything on that
        self.main_dir = os.path.abspath(self.main_dir)
        os.makedirs(self.main_dir, exist_ok=True)
        os.environ['GST_VALIDATE_LAUNCHER_MAIN_DIR'] = self.main_dir

        # default for output_dir is MAINDIR
        if not self.output_dir:
            self.output_dir = self.main_dir
        else:
            self.output_dir = os.path.abspath(self.output_dir)

        self.mute = not self.unmute
        if self.gdb_non_stop:
            self.gdb = True

        if self.gdb:
            self.logsdir = "stdout"
            self.debug = True
            self.num_jobs = 1

        if self.xunit_file:
            self.keep_logs = True

        # other output directories
        if self.logsdir in ['stdout', 'stderr']:
            # Allow -l stdout/stderr to work like -rl stdout/stderr
            self.redirect_logs = self.logsdir
            self.logsdir = None
        if self.verbose:
            self.redirect_logs = 'stdout'
            self.logsdir = None
        if self.logsdir is None:
            self.logsdir = os.path.join(self.output_dir, "logs")
        if self.dest is None:
            self.dest = os.path.join(self.output_dir, "rendered")
        self.privatedir = os.path.join(self.output_dir, "launcher-private")

        destparsed = urllib.parse.urlparse(self.dest)
        if destparsed.scheme == "" or destparsed.scheme == "file":
            os.makedirs(destparsed.path, exist_ok=True)
            if destparsed.scheme == "":
                self.dest = path2url(self.dest)
        if not os.path.exists(self.logsdir):
            os.makedirs(self.logsdir)
        if not os.path.exists(self.privatedir):
            os.makedirs(self.privatedir)

        if self.redirect_logs not in ['stdout', 'stderr', False]:
            printc("Log redirection (%s) must be either 'stdout' or 'stderr'."
                   % self.redirect_logs, Colors.FAIL, True)
            return False

        if self.no_color:
            utils.desactivate_colors()
        if self.clone_dir is None:
            if not utils.USING_SUBPROJECT:
                self.clone_dir = os.path.join(self.main_dir, QA_ASSETS)
            else:
                self.clone_dir = self.main_dir

        if not isinstance(self.paths, list):
            self.paths = [self.paths]

        if not isinstance(self.user_paths, list):
            self.user_paths = [self.user_paths]

        self.paths = list(set(self.paths).union(set(self.user_paths)))

        if self.generate_info_full is True:
            self.generate_info = True

        if not utils.USING_SUBPROJECT:
            if self.sync_all is True or self.force_sync is True:
                self.sync = True

            if not self.sync and not os.path.exists(self.clone_dir) and \
                    self.clone_dir == os.path.join(self.clone_dir, MEDIAS_FOLDER):
                printc("Media path (%s) does not exists. Forgot to run --sync ?"
                       % self.clone_dir, Colors.FAIL, True)
                return False

        if (self.main_dir != DEFAULT_MAIN_DIR or self.clone_dir != QA_ASSETS):
            local_clone_dir = os.path.join(
                self.main_dir, self.clone_dir, "testsuites")
            if local_clone_dir not in self.testsuites_dirs:
                self.testsuites_dirs.insert(0, local_clone_dir)
        if self.valgrind:
            try:
                subprocess.check_output("valgrind --help", shell=True)
            except subprocess.CalledProcessError:
                printc("Want to use valgrind, but not available on the system",
                       Colors.FAIL)
                return False

        if self.rr:
            if not shutil.which('rr'):
                printc("Want to use rr, but not available on the system",
                       Colors.FAIL)
                return False

        if self.html:
            try:
                import commonmark
            except ImportError:
                printc("You want to output html logs but commonmark not found. Install it"
                       " with `pip install commonmark` and try again.", Colors.FAIL)
                return False

        return True

    def set_http_server_dir(self, path):
        if self.http_server_dir is not None:
            printc("Server directory already set to %s" % self.http_server_dir)
            return

        self.http_server_dir = path

    def add_paths(self, paths, force=False):
        if force is False:
            if self.paths:
                return
        if not isinstance(paths, list):
            paths = [paths]

        if self._using_default_paths:
            self.paths = paths
            self._using_default_paths = False
        else:
            for path in paths:
                if path not in self.paths:
                    self.paths.append(path)

    @staticmethod
    def create_parser():
        parser = argparse.ArgumentParser(
            formatter_class=argparse.RawTextHelpFormatter,
            prog='gst-validate-launcher', description=HELP)

        parser.add_argument('testsuites', metavar='N', nargs='*',
                            help="""Lets you specify a test to run, a testsuite name or a file where the testsuite to execute is defined.

    In the module if you want to work with a specific test manager(s) (for example,
    'ges' or 'validate'), you should define the TEST_MANAGER variable in the
    testsuite file (it can be a list of test manager names)

    In this file you should implement a setup_tests function. That function takes
    a TestManager and the GstValidateLauncher option as parameters and return True
    if it succeeded loading the tests, False otherwise.
    You will be able to configure the TestManager with its various methods. This
    function will be called with each TestManager usable, for example you will be
    passed the 'validate' TestManager in case the GstValidateManager launcher is
    available. You should configure it using:

    * test_manager.add_scenarios: which allows you to register a list of scenario names to be run
    * test_manager.set_default_blacklist: Lets you set a list of tuple of the form:
            (@regex_defining_blacklister_test_names, @reason_for_the_blacklisting)
    * test_manager.add_generators: which allows you to register a list of #GstValidateTestsGenerator
        to be used to generate tests
    * test_manager.add_encoding_formats:: which allows you to register a list #MediaFormatCombination to be used for transcoding tests

    You can also set default values with:
        * test_manager.register_defaults: Sets default values for all parametters
        * test_manager.register_default_test_generators: Sets default values for the TestsGenerators to be used
        * test_manager.register_default_scenarios: Sets default values for the scenarios to be executed
        * test_manager.register_default_encoding_formats: Sets default values for the encoding formats to be tested

    Note that all testsuite should be inside python modules, so the directory should contain a __init__.py file
    """,
                            default=["validate"])
        parser.add_argument("-d", "--debug", dest="debug",
                            action="store_true",
                            help="Let user debug the process on timeout")
        parser.add_argument("--timeout-factor", dest="timeout_factor",
                            default=1.0, type=float,
                            help="Factor to be applied on all timeout values.")
        parser.add_argument("-f", "--forever", dest="forever",
                            action="store_true",
                            help="Keep running tests until one fails")
        parser.add_argument("--n-runs", dest="n_runs", action='store',
                            help="Number of runs, if the testsuites."
                            " Meaning no failure will stop the testuite"
                            " run meanwhile.", type=int),
        parser.add_argument("-F", "--fatal-error", dest="fatal_error",
                            action="store_true",
                            help="Stop on first fail")
        parser.add_argument("--fail-on-testlist-change",
                            dest="fail_on_testlist_change",
                            action="store_true",
                            help="Fail the testsuite if a test has been added"
                            " or removed without being explicitely added/removed "
                            "from the testlist file.")
        parser.add_argument("-t", "--wanted-tests", dest="wanted_tests",
                            action="append",
                            help="Define the tests to execute, it can be a regex."
                            " If it contains defaults_only, only default scenarios"
                            " will be executed")
        parser.add_argument("-b", "--blacklisted-tests", dest="blacklisted_tests",
                            action="append",
                            help="Define the tests not to execute, it can be a regex.")
        parser.add_argument("--check-bugs", dest="check_bugs_status",
                            action="store_true",
                            help="Check if the bug linked to blacklisted tests has"
                            " been marked as resolved. (works with gitlab and bugzilla)")
        parser.add_argument("-L", "--list-tests",
                            dest="list_tests",
                            action="store_true",
                            help="List tests and exit")
        parser.add_argument("--unmute", dest="unmute",
                            action="store_true",
                            help="Unmute playback output, which means that we use "
                            "'real' sinks")
        parser.add_argument("-m", "--mute", dest="mute",
                            action="store_true",
                            help="Mute playback output, which means that we use "
                            "a fakesink")
        parser.add_argument("-n", "--no-color", dest="no_color",
                            action="store_true",
                            help="Set it to output no colored text in the terminal")
        parser.add_argument("-g", "--generate-media-info", dest="generate_info",
                            action="store_true",
                            help="Set it in order to generate the missing .media_infos files")
        parser.add_argument("--update-media-info", dest="update_media_info",
                            action="store_true",
                            help="Set it in order to update existing .media_infos files")
        parser.add_argument(
            "-G", "--generate-media-info-with-frame-detection", dest="generate_info_full",
            action="store_true",
            help="Set it in order to generate the missing .media_infos files. "
            "It implies --generate-media-info but enabling frame detection")
        parser.add_argument("-lt", "--long-test-limit", dest="long_limit",
                            action='store',
                            help="Defines the limit for which a test is considered as long (in seconds)."
                            " Note that 0 will enable all tests", type=int),
        parser.add_argument("--dump-on-failure", dest="dump_on_failure",
                            action="store_true", default=False,
                            help="Dump logs to stdout when a test fails."
                            " Note that bat is used to enhance output if available"
                            " (See https://github.com/sharkdp/bat)")
        parser.add_argument("--max-dump-size", dest="max_dump_size", type=float,
                            default=0.5, help="Maximum size of logs to dump on stdout in MB.")
        parser.add_argument("-c", "--config", dest="config",
                            help="This is DEPRECATED, prefer using the testsuite format"
                            " to configure testsuites")
        parser.add_argument("-vg", "--valgrind", dest="valgrind",
                            action="store_true",
                            help="Run the tests inside Valgrind")
        parser.add_argument("-rr", "--rr", dest="rr",
                            action="store_true",
                            help="Run the tests inside rr record")
        parser.add_argument("--gdb", dest="gdb",
                            action="store_true",
                            help="Run the tests inside gdb (implies"
                            " --output-dir=stdout and --jobs=1)")
        parser.add_argument("--gdb-non-stop", dest="gdb_non_stop",
                            action="store_true",
                            help="Run the test automatically in gdb (implies --gdb)")
        parser.add_argument("-nd", "--no-display", dest="no_display",
                            action="store_true",
                            help="Run the tests without outputting graphics"
                            " on any display. It tries to run all graphical operation"
                            " in a virtual framebuffer."
                            " Note that it is currently implemented only"
                            " for the X  server thanks to Xvfb (which is requeried in that case)")
        parser.add_argument('--xunit-file', dest='xunit_file',
                            action='store', metavar="FILE",
                            help=("Path to xml file to store the xunit report in."))
        parser.add_argument('--shuffle', dest="shuffle", action="store_true",
                            help="Runs the test in a random order. Can help speed up the overall"
                            " test time by running synchronized and unsynchronized tests"
                            " at the same time")
        parser.add_argument('--retry-on-failures', dest="retry_on_failures", action="store_true",
                            help="Re-try tests that produce unexpected results")
        parser.add_argument('--no-retry-on-failures', dest="no_retry_on_failures", action="store_true",
                            help="Disable retrying on failure, event for known to be flaky tests.")
        parser.add_argument('--html', dest="html", action="store_true",
                            help="Write logs as html")
        parser.add_argument("--keep-logs", dest="keep_logs",
                            action="store_true",
                            help="Keep the logs in the output directory on success, by default logs are removed unless the test passes")
        dir_group = parser.add_argument_group(
            "Directories and files to be used by the launcher")
        dir_group.add_argument("-M", "--main-dir", dest="main_dir",
                               help="Main directory where to put files."
                               " Respects the GST_VALIDATE_LAUNCHER_MAIN_DIR environment variable."
                               " Default is %s" % DEFAULT_MAIN_DIR)
        dir_group.add_argument("--testsuites-dir", dest="testsuites_dirs", action='append',
                               help="Directory where to look for testsuites. Default is %s"
                               % utils.DEFAULT_TESTSUITES_DIRS)
        dir_group.add_argument("-o", "--output-dir", dest="output_dir",
                               help="Directory where to store logs and rendered files. Default is MAIN_DIR")
        dir_group.add_argument("-l", "--logs-dir", dest="logsdir",
                               help="Directory where to store logs, default is OUTPUT_DIR/logs.")
        dir_group.add_argument("-R", "--render-path", dest="dest",
                               help="Set the path to which projects should be rendered, default is OUTPUT_DIR/rendered")
        dir_group.add_argument("-p", "--medias-paths", dest="user_paths", action="append",
                               help="Paths in which to look for media files")
        dir_group.add_argument("-a", "--clone-dir", dest="clone_dir",
                               help="Paths where to clone the testuite to run."
                               " default is MAIN_DIR/gst-integration-testsuites")
        dir_group.add_argument("-rl", "--redirect-logs", dest="redirect_logs",
                               help="Redirect logs to 'stdout' or 'stderr'.")
        dir_group.add_argument("-v", "--verbose", dest="verbose",
                               action='count',
                               help="Redirect logs to stdout.")
        dir_group.add_argument("-j", "--jobs", dest="num_jobs",
                               help="Number of tests to execute simultaneously"
                               " (Defaults to the number of cores of the processor)",
                               type=_positive_integer_type)
        dir_group.add_argument("--ignore-numfailures", dest="ignore_numfailures",
                               help="Ignore the number of failed test in exit code",
                               default=False, action='store_true')
        dir_group.add_argument("--parts", dest="num_parts",
                               help="Splits the tests in equally distributed parts and only run one part"
                               " (Defaults to 1 part)",
                               type=int, default=1)
        dir_group.add_argument("--part-index", dest="part_index",
                               help="The index of the part to be run (starts at 1).",
                               type=int, default=1)

        http_server_group = parser.add_argument_group(
            "Handle the HTTP server to be created")
        http_server_group.add_argument(
            "--http-server-port", dest="http_server_port",
            help="Port on which to run the http server on localhost", type=int)
        http_server_group.add_argument(
            "--http-bandwith-limitation", dest="http_bandwith",
            help="The artificial bandwith limitation to introduce to the local server (in Bytes/sec) (default: 1 MBps)")
        http_server_group.add_argument(
            "-s", "--folder-for-http-server", dest="http_server_dir",
            help="Folder in which to create an http server on localhost. Default is PATHS")
        http_server_group.add_argument("--http-only", dest="httponly",
                                       action='store_true',
                                       help="Start the http server and quit")

        assets_group = parser.add_argument_group("Handle remote assets")
        assets_group.add_argument(
            "--get-assets-command", dest="get_assets_command",
            help="Command to get assets")
        assets_group.add_argument("--remote-assets-url", dest="remote_assets_url",
                                  help="Url to the remote assets (default:%s)" % DEFAULT_GST_QA_ASSETS_REPO)
        assets_group.add_argument("-S", "--sync", dest="sync", action="store_true",
                                  help="Synchronize asset repository")
        assets_group.add_argument("-fs", "--force-sync", dest="force_sync", action="store_true",
                                  help="Synchronize asset repository reseting any change that might have"
                                  " happened in the testsuite")
        assets_group.add_argument("--sync-all", dest="sync_all", action="store_true",
                                  help="Synchronize asset repository,"
                                  " including big media files")
        assets_group.add_argument("--sync-version", dest="sync_version",
                                  help="Version of the asset repository, default is GST_VALIDATE_TESTSUITE_VERSION")
        assets_group.add_argument("--usage", action=PrintUsage,
                                  help="Print usage documentation")
        return parser


def setup_launcher_from_args(args, main_options=None):
    loggable.init("GST_VALIDATE_LAUNCHER_DEBUG", True, False)
    parser = LauncherConfig.create_parser()
    tests_launcher = _TestsLauncher()
    tests_launcher.add_options(parser)

    if "--help" in sys.argv and which(LESS):
        tmpf = tempfile.NamedTemporaryFile(mode='r+')

        parser.print_help(file=tmpf)
        os.system("%s %s" % (LESS, tmpf.name))
        return False, None, None

    options = LauncherConfig()
    parser.parse_args(args=args, namespace=options)
    if main_options:
        # Override output directories and logging properties of the sub launcher.
        for option in ["main_dir", "output_dir", "logsdir", "dest", "clone_dir",
                       "redirect_logs", "verbose", "timeout_factor"]:
            setattr(options, option, getattr(main_options, option))
    if not options.cleanup():
        return False, None, None

    if options.remote_assets_url and options.sync and not os.path.exists(options.clone_dir):
        if not download_assets(options):
            return False, None, None
    # Ensure that the scenario manager singleton is ready to be used
    ScenarioManager().config = options
    if not tests_launcher.set_settings(options, []):
        return False, None, None

    return True, options, tests_launcher


def main(libsdir):
    global LIBSDIR
    LIBSDIR = libsdir

    utils.DEFAULT_TESTSUITES_DIRS.append(os.path.join(LIBSDIR, "testsuites"))
    os.environ["GST_VALIDATE_APPS_DIR"] = os.path.join(
        LIBSDIR, "apps") + os.pathsep + os.environ.get("GST_VALIDATE_APPS_DIR", "")

    res, options, tests_launcher = setup_launcher_from_args(sys.argv[1:])
    if res is False:
        return 1

    if options.list_tests:
        if tests_launcher.list_tests() == -1:
            printc("\nFailling as tests have been removed/added "
                   " (--fail-on-testlist-change)", Colors.FAIL)
            return 1

        tests = tests_launcher.tests
        for test in tests:
            printc(test)

        printc("\nNumber of tests: %d" % len(tests), Colors.OKGREEN)
        return 0

    if options.httponly is True:
        print("Running HTTP server only")
        return 0

    # There seems to be some issue with forking, dconf and some gtype
    # initialization that deadlocks occasionally, setting the
    # GSettings backend make it go away.
    # Also happened here:
    # https://cgit.freedesktop.org/gstreamer/gst-plugins-good/commit/tests/check/Makefile.am?id=8e2c1d1de56bddbff22170f8b17473882e0e63f9
    os.environ['GSETTINGS_BACKEND'] = "memory"

    exception = None
    try:
        tests_launcher.run_tests()
    except Exception as e:
        exception = e
        pass
    finally:
        res = tests_launcher.final_report()
        if options.ignore_numfailures:
            res = 0
        if exception is not None:
            raise exception

    return res
