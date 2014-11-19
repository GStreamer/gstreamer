#!/usr/bin/env python2
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
import utils
import urlparse
import loggable
import argparse
import reporters
import subprocess


from httpserver import HTTPServer
from baseclasses import _TestsLauncher, ScenarioManager
from utils import printc, path2url, DEFAULT_MAIN_DIR, launch_command, Colors, Protocols


HELP = '''

===============================================================================
                       gst-validate-launcher
===============================================================================

1. Introduction
----------------

gst-validate-launcher is a test launcher tool, it has been designed to
launch the various tools included in GstValidate running tests on real
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

A default suite of tests is provided and you can run it pretty simply doing:

.    $gst-validate-launch --sync

That will download Gstreamer upstream default assets into the
default folder (%s) and run all currently
activated tests. Note that we use git-annex https://git-annex.branchable.com/ so
you will need that tool to get started.

3. Implement your own tests
---------------------------

To implement new tests, you will just need to set the media path using the
--medias-paths argument. If you want to run all avalaible scenarios on all the
file present in that folder, you should run the first time:

.    $gst-validate-launch --medias-paths /path/to/media/files --generate-media-info

That will generate the .media_info files that contain informations about the media
files present in that folder. Those media_info file are simple XML file describing
the topology of the media files. You should not reuse the --generate-media-info
next times. The generated media files will be used as a reference for following
runs. You might want to check that they contain the right informations yourself
the first time.

Those .media_info are the files that are used by gst-validate-launcher to know
what media files can be used for the different scenarios. For example if a
file is not seekable, seeking scenarios will not be run on it etc...

3.1 Scenarios specific to a media file/stream:
----------------------------------------------

It is possible that some scenarios are very specific to one media file, in that case,
the .scenario file should be present in the same folder as the .media_info file and
be called similarly. For example for a file called /some/media/file.mp4, the media_info
file will be called /some/media/file.mp4 and a scenario that will seek to a position that
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
It uses the same synthax as PITIVI_DEBUG (more information at:
http://wiki.pitivi.org/wiki/Bug_reporting#Debug_logs).
''' % ("\n  * ".join([reporter.name for reporter in
                      utils.get_subclasses(reporters.Reporter, reporters.__dict__)]
                     ),
       DEFAULT_MAIN_DIR,
       "\n  * ".join([getattr(Protocols, att) for att in
                      dir(Protocols) if isinstance(getattr(Protocols, att), str)
                      and not att.startswith("_")]))

QA_ASSETS = "gst-qa-assets"
MEDIAS_FOLDER = "medias"
DEFAULT_GST_QA_ASSETS_REPO = "git://people.freedesktop.org/~tsaunier/gst-qa-assets/"


def update_assets(options):
    try:
        launch_command("cd %s && %s" % (options.clone_dir,
                                        options.update_assets_command),
                       fails=True)
    except subprocess.CalledProcessError as e:
        if "annex" in options.update_assets_command:
            m = "\n\nMAKE SURE YOU HAVE git-annex INSTALLED!"
        else:
            m = ""

        printc("Could not update assets repository\n\nError: %s%s" % (e, m),
               Colors.FAIL, True)

        return False

    return True


def download_assets(options):
    try:
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


def main(libsdir):
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawTextHelpFormatter,
        prog='gst-validate-launcher', description=HELP)
    parser.add_argument("-d", "--debug", dest="debug",
                        action="store_true",
                        default=False,
                        help="Let user debug the process on timeout")
    parser.add_argument("-f", "--forever", dest="forever",
                        action="store_true", default=False,
                        help="Keep running tests until one fails")
    parser.add_argument("-F", "--fatal-error", dest="fatal_error",
                        action="store_true", default=False,
                        help="Stop on first fail")
    parser.add_argument("-t", "--wanted-tests", dest="wanted_tests",
                        default=[],
                        action="append",
                        help="Define the tests to execute, it can be a regex"
                        " if it contains defaults_only, only default scenarios"
                        " will be executed")
    parser.add_argument("-b", "--blacklisted-tests", dest="blacklisted_tests",
                        default=[],
                        action="append",
                        help="Define the tests not to execute, it can be a regex.")
    parser.add_argument("-L", "--list-tests",
                        dest="list_tests",
                        action="store_true",
                        default=False,
                        help="List tests and exit")
    parser.add_argument("-m", "--mute", dest="mute",
                        action="store_true", default=False,
                        help="Mute playback output, which mean that we use "
                        "a fakesink")
    parser.add_argument("-n", "--no-color", dest="no_color",
                        action="store_true", default=False,
                        help="Set it to output no colored text in the terminal")
    parser.add_argument("-g", "--generate-media-info", dest="generate_info",
                        action="store_true", default=False,
                        help="Set it in order to generate the missing .media_infos files")
    parser.add_argument("--update-media-info", dest="update_media_info",
                        action="store_true", default=False,
                        help="Set it in order to update exising .media_infos files")
    parser.add_argument(
        "-G", "--generate-media-info-with-frame-detection", dest="generate_info_full",
        action="store_true", default=False,
        help="Set it in order to generate the missing .media_infos files"
        "It implies --generate-media-info but enabling frame detection")
    parser.add_argument("-lt", "--long-test-limit", dest="long_limit",
                        default=utils.LONG_TEST, action='store',
                        help="Defines the limite from which a test is concidered as long (in seconds)"),
    parser.add_argument("-c", "--config", dest="config",
                        default=None,
                        help="""Lets you specify a file where the testsuite to execute is defined.
In this file you will have acces to the TestManager objects that you can configure with
its various methods, for example you can find the 'validate' variable in case the GstValidateManager
launcher is avalaible. You should configure it using:
   * validate.add_scenarios: which allows you to register a list of scenario names to be run
   * validate.set_default_blacklist: Lets you set a list of tuple of the form:
         (@regex_defining_blacklister_test_names, @reason_for_the_blacklisting)
   * validate.add_generators: which allows you to register a list of #GstValidateTestsGenerator
     to be used to generate tests
   * validate.add_encoding_formats:: which allows you to register a list #MediaFormatCombination to be used for transcoding tests

You can also set default values with:
    * validate.register_defaults: Sets default values for all parametters
    * validate.register_default_test_generators: Sets default values for the TestsGenerators to be used
    * gst_validate_register_default_scenarios: Sets default values for the scenarios to be executed
    * gst_validate_register_default_encoding_formats: Sets default values for the encoding formats to be tested

Note: In the config file, you have acces to the options variable resulting from the parsing of the command line
user argument, you can thus overrides command line options using that.
""")
    dir_group = parser.add_argument_group(
        "Directories and files to be used by the launcher")
    parser.add_argument('--xunit-file', action='store',
                        dest='xunit_file', metavar="FILE",
                        default=None,
                        help=("Path to xml file to store the xunit report in. "
                              "Default is LOGSDIR/xunit.xml"))
    dir_group.add_argument("-M", "--main-dir", dest="main_dir",
                           default=DEFAULT_MAIN_DIR,
                           help="Main directory where to put files. Default is %s" % DEFAULT_MAIN_DIR)
    dir_group.add_argument("-o", "--output-dir", dest="output_dir",
                           default=None,
                           help="Directory where to store logs and rendered files. Default is MAIN_DIR")
    dir_group.add_argument("-l", "--logs-dir", dest="logsdir",
                           default=None,
                           help="Directory where to store logs, default is OUTPUT_DIR/logs."
                           " Note that 'stdout' and 'sdterr' are valid values that lets you get all the logs"
                           " printed in the terminal")
    dir_group.add_argument("-R", "--render-path", dest="dest",
                           default=None,
                           help="Set the path to which projects should be rendered, default is OUTPUT_DIR/rendered")
    dir_group.add_argument(
        "-p", "--medias-paths", dest="paths", action="append",
        default=None,
        help="Paths in which to look for media files, default is MAIN_DIR/gst-qa-assets/media")
    dir_group.add_argument("-a", "--clone-dir", dest="clone_dir",
                           default=None,
                           help="Paths in which to look for media files, default is MAIN_DIR/gst-qa-assets")

    http_server_group = parser.add_argument_group(
        "Handle the HTTP server to be created")
    http_server_group.add_argument(
        "--http-server-port", dest="http_server_port",
        default=8079,
        help="Port on which to run the http server on localhost")
    http_server_group.add_argument(
        "--http-bandwith-limitation", dest="http_bandwith",
        default=1024 * 1024,
        help="The artificial bandwith limitation to introduce to the local server (in Bytes/sec) (default: 1 MBps)")
    http_server_group.add_argument(
        "-s", "--folder-for-http-server", dest="http_server_dir",
        default=None,
        help="Folder in which to create an http server on localhost. Default is PATHS")
    http_server_group.add_argument("--http-only", dest="httponly",
                                   default=False, action='store_true',
                                   help="Start the http server and quit")

    assets_group = parser.add_argument_group("Handle remote assets")
    assets_group.add_argument(
        "-u", "--update-assets-command", dest="update_assets_command",
        default="git fetch origin && git checkout origin/master && git annex get .",
        help="Command to update assets")
    assets_group.add_argument(
        "--get-assets-command", dest="get_assets_command",
        default="git clone",
        help="Command to get assets")
    assets_group.add_argument("--remote-assets-url", dest="remote_assets_url",
                              default=DEFAULT_GST_QA_ASSETS_REPO,
                              help="Url to the remote assets (default:%s)" % DEFAULT_GST_QA_ASSETS_REPO)
    assets_group.add_argument("-S", "--sync", dest="sync", action="store_true",
                              default=False, help="Synchronize asset repository")
    assets_group.add_argument("--usage", dest="sync", action=PrintUsage,
                              help="Print usage documentation")

    loggable.init("GST_VALIDATE_LAUNCHER_DEBUG", True, False)

    tests_launcher = _TestsLauncher(libsdir)
    tests_launcher.add_options(parser)

    (options, args) = parser.parse_known_args()

    # Get absolute path for main_dir and base everything on that
    options.main_dir = os.path.abspath(options.main_dir)

    # default for output_dir is MAINDIR
    if not options.output_dir:
        options.output_dir = options.main_dir
    else:
        options.output_dir = os.path.abspath(options.output_dir)

    # other output directories
    if options.logsdir is None:
        options.logsdir = os.path.join(options.output_dir, "logs")
    if options.xunit_file is None:
        options.xunit_file = os.path.join(options.logsdir, "xunit.xml")
    if options.dest is None:
        options.dest = os.path.join(options.output_dir, "rendered")

    if not os.path.exists(options.dest):
        os.makedirs(options.dest)
    if urlparse.urlparse(options.dest).scheme == "":
        options.dest = path2url(options.dest)

    if options.no_color:
        utils.desactivate_colors()
    if options.clone_dir is None:
        options.clone_dir = os.path.join(options.main_dir, QA_ASSETS)
    if options.paths is None:
        options.paths = os.path.join(options.clone_dir, MEDIAS_FOLDER)

    if options.generate_info_full is True:
        options.generate_info = True

    if options.http_server_dir is None:
        if isinstance(options.paths, list):
            options.http_server_dir = options.paths[0]
        else:
            options.http_server_dir = options.paths

    if not options.sync and not os.path.exists(options.clone_dir) and \
            options.clone_dir == os.path.join(options.clone_dir, MEDIAS_FOLDER):
        printc("Media path (%s) does not exists. Forgot to run --sync ?"
               % options.clone_dir, Colors.FAIL, True)
        return -1

    tests_launcher.set_settings(options, args)

    if options.remote_assets_url and options.sync:
        if os.path.exists(options.clone_dir):
            if not update_assets(options):
                exit(1)
        else:
            if not download_assets(options):
                exit(1)

            if not update_assets(options):
                exit(1)

    # Ensure that the scenario manager singleton is ready to be used
    ScenarioManager().config = options
    tests_launcher.list_tests()

    if options.list_tests:
        l = tests_launcher.tests
        for test in l:
            printc(test)

        printc("\nNumber of tests: %d" % len(l), Colors.OKGREEN)
        return 0

    httpsrv = HTTPServer(options)
    if tests_launcher.needs_http_server() or options.httponly is True:
        httpsrv.start()

    if options.httponly is True:
        print "Running HTTP server only"
        return

    e = None
    try:
        tests_launcher.run_tests()
    except Exception as e:
        pass
    finally:
        tests_launcher.final_report()
        httpsrv.stop()
        if e is not None:
            raise

    return 0
