#!/usr//bin/python
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
import utils
import urlparse
import loggable
from optparse import OptionParser, OptionGroup

from httpserver import HTTPServer
from baseclasses import _TestsLauncher, ScenarioManager
from utils import printc, path2url, DEFAULT_MAIN_DIR, launch_command, Colors


QA_ASSETS = "gst-qa-assets"
DEFAULT_GST_QA_ASSETS_REPO = "git://people.freedesktop.org/~tsaunier/gst-qa-assets/"

def main():
    parser = OptionParser()
    # FIXME:
    #parser.add_option("-g", "--gdb", dest="gdb",
                      #action="store_true",
                      #default=False,
                      #help="Run applications into gdb")
    parser.add_option("-f", "--forever", dest="forever",
                      action="store_true", default=False,
                      help="Keep running tests until one fails")
    parser.add_option("-F", "--fatal-error", dest="fatal_error",
                      action="store_true", default=False,
                      help="Stop on first fail")
    parser.add_option("-t", "--wanted-tests", dest="wanted_tests",
                      default=[],
                      action="append",
                      help="Define the tests to execute, it can be a regex")
    parser.add_option("-b", "--blacklisted-tests", dest="blacklisted_tests",
                      default=[],
                      action="append",
                      help="Define the tests not to execute, it can be a regex.")
    parser.add_option("-L", "--list-tests",
                      dest="list_tests",
                      action="store_true",
                      default=False,
                      help="List tests and exit")
    parser.add_option("-m", "--mute", dest="mute",
                      action="store_true", default=False,
                      help="Mute playback output, which mean that we use "
                      "a fakesink")
    parser.add_option("-n", "--no-color", dest="no_color",
                     action="store_true", default=False,
                     help="Set it to output no colored text in the terminal")
    parser.add_option("-g", "--generate-media-info", dest="generate_info",
                     action="store_true", default=False,
                     help="Set it in order to generate the missing .media_infos files")

    dir_group = OptionGroup(parser, "Directories and files to be used by the launcher")
    parser.add_option('--xunit-file', action='store',
                      dest='xunit_file', metavar="FILE",
                      default=None,
                      help=("Path to xml file to store the xunit report in. "
                      "Default is xunit.xml in the logs-dir directory"))
    dir_group.add_option("-M", "--main-dir", dest="main_dir",
                      default=DEFAULT_MAIN_DIR,
                      help="Main directory where to put files."
                      "Logs will land into 'main_dir'" + os.pathsep + "'logs' and"
                      "assets will be cloned into 'main_dir'" + os.pathsep + "'gst-qa-assets'"
                      " if not explicitely changed")
    dir_group.add_option("-o", "--output-dir", dest="outputdir",
                      action="store_true", default=os.path.join(DEFAULT_MAIN_DIR,
                      "logs"),
                      help="Directory where to store logs and rendered files")
    dir_group.add_option("-l", "--logs-dir", dest="logsdir",
                      default=None,
                      help="Directory where to store logs, default is logs/ in "
                      "--output-dir result")
    dir_group.add_option("-R", "--render-path", dest="dest",
                     default=None,
                     help="Set the path to which projects should be rendered")
    dir_group.add_option("-p", "--medias-paths", dest="paths", action="append",
                      default=None,
                      help="Paths in which to look for media files, will be %s "
                      "if nothing specified" %
                      os.path.join(DEFAULT_MAIN_DIR, QA_ASSETS, "medias"))
    dir_group.add_option("", "--clone-dir", dest="clone_dir",
                      default=None,
                      help="Paths in which to look for media files, will be %s "
                      "if nothing specified" %
                      [os.path.join(DEFAULT_MAIN_DIR, QA_ASSETS)])
    parser.add_option_group(dir_group)

    http_server_group = OptionGroup(parser, "Handle the HTTP server to be created")
    http_server_group.add_option("", "--http-server-port", dest="http_server_port",
                      default=8079,
                      help="Port on which to run the http server on localhost")
    http_server_group.add_option("-s", "--folder-for-http-server", dest="http_server_dir",
                      default=os.path.join(DEFAULT_MAIN_DIR, QA_ASSETS, "medias"),
                      help="Folder in which to create an http server on localhost")
    parser.add_option_group(http_server_group)

    assets_group = OptionGroup(parser, "Handle remote assets")
    assets_group.add_option("-u", "--update-assets-command", dest="update_assets_command",
                      default="git pull --rebase",
                      help="Command to update assets")
    assets_group.add_option("", "--get-assets-command", dest="get_assets_command",
                      default="git clone",
                      help="Command to get assets")
    assets_group.add_option("", "--remote-assets-url", dest="remote_assets_url",
                      default=DEFAULT_GST_QA_ASSETS_REPO,
                      help="Url to the remote assets")
    parser.add_option_group(assets_group)

    loggable.init("GST_VALIDATE_LAUNCHER_DEBUG", True, False)

    tests_launcher = _TestsLauncher()
    tests_launcher.add_options(parser)

    blacklisted = tests_launcher.get_blacklisted()
    if blacklisted:
        msg = "Currently 'hardcoded' blacklisted tests:\n"
        for name, bug in blacklisted:
            sys.argv.extend(["-b", name])
            msg += "    + %s -- bug: %s\n" % (name, bug)

        printc(msg, Colors.FAIL, True)

    (options, args) = parser.parse_args()
    if options.logsdir is None:
        options.logsdir = os.path.join(options.outputdir, "logs")
    if options.xunit_file is None:
        options.xunit_file = os.path.join(options.logsdir, "xunit.xml")

    if options.dest is None:
        options.dest = os.path.join(options.outputdir, "rendered")
    if not os.path.exists(options.dest):
        os.makedirs(options.dest)
    if urlparse.urlparse(options.dest).scheme == "":
        options.dest = path2url(options.dest)

    if options.no_color:
        utils.desactivate_colors()
    if options.clone_dir is None:
        options.clone_dir = os.path.join(DEFAULT_MAIN_DIR, QA_ASSETS)
    if options.paths is None:
        options.paths = options.clone_dir

    tests_launcher.set_settings(options, args)

    if options.remote_assets_url:
        if os.path.exists(options.clone_dir):
            launch_command("cd %s && %s" % (options.clone_dir,
                                            options.update_assets_command))
        else:
            launch_command("%s %s %s" % (options.get_assets_command,
                                         options.remote_assets_url,
                                         options.clone_dir))

    # Ensure that the scenario manager singleton is ready to be used
    ScenarioManager().config = options
    tests_launcher.list_tests()

    httpsrv = HTTPServer(options)
    if tests_launcher.needs_http_server():
        httpsrv.start()

    e = None
    try:
        if options.list_tests:
            for test in tests_launcher.tests:
                printc(test)
            return 0

        tests_launcher.run_tests()
        tests_launcher.final_report()
    except Exception as e:
        pass
    finally:
        httpsrv.stop()
        if e is not None:
            raise

    return 0
