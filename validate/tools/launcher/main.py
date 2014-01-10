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
import utils
import urlparse
import loggable
from optparse import OptionParser

from baseclasses import _TestsLauncher
from utils import printc, path2url, DEFAULT_GST_QA_ASSETS, launch_command

DEFAULT_GST_QA_ASSETS_REPO = "git://people.freedesktop.org/~tsaunier/gst-qa-assets/"


def main():
    parser = OptionParser()
    # FIXME:
    #parser.add_option("-g", "--gdb", dest="gdb",
                      #action="store_true",
                      #default=False,
                      #help="Run applications into gdb")
    #parser.add_option("-f", "--forever", dest="forever",
                      #action="store_true", default=False,
                      #help="Keep running tests until one fails")
    #parser.add_option("-F", "--fatal-error", dest="fatal_error",
                      #action="store_true", default=False,
                      #help="Stop on first fail")
    parser.add_option('--xunit-file', action='store',
                      dest='xunit_file', metavar="FILE",
                      default=None,
                      help=("Path to xml file to store the xunit report in. "
                      "Default is xunit.xml the logs-dir directory"))
    parser.add_option("-t", "--wanted-tests", dest="wanted_tests",
                      default=None,
                      help="Define the tests to execute, it can be a regex")
    parser.add_option("-L", "--list-tests",
                      dest="list_tests",
                      action="store_true",
                      default=False,
                      help="List tests and exit")
    parser.add_option("-l", "--logs-dir", dest="logsdir",
                      action="store_true", default=os.path.expanduser("~/gst-validate/logs/"),
                      help="Directory where to store logs")
    parser.add_option("-p", "--medias-paths", dest="paths",
                      default=[os.path.join(DEFAULT_GST_QA_ASSETS, "medias")],
                      help="Paths in which to look for media files")
    parser.add_option("-m", "--mute", dest="mute",
                      action="store_true", default=False,
                      help="Mute playback output, which mean that we use "
                      "a fakesink")
    parser.add_option("-o", "--output-path", dest="dest",
                     default=None,
                     help="Set the path to which projects should be"
                     " renderd")
    parser.add_option("-n", "--no-color", dest="no_color",
                     action="store_true", default=False,
                     help="Set it to output no colored text in the terminal")
    loggable.init("GST_VALIDATE_LAUNCHER_DEBUG", True, False)

    tests_launcher = _TestsLauncher()
    tests_launcher.add_options(parser)
    (options, args) = parser.parse_args()
    if options.xunit_file is None:
        options.xunit_file = os.path.join(options.logsdir, "xunit.xml")
    if options.dest is None:
        options.dest = os.path.join(options.logsdir, "rendered")
    if not os.path.exists(options.dest):
        os.makedirs(options.dest)
    if urlparse.urlparse(options.dest).scheme == "":
        options.dest = path2url(options.dest)
    if options.no_color:
        utils.desactivate_colors()

    tests_launcher.set_settings(options, args)

    if options.list_tests:
        for test in tests_launcher.tests:
            printc(test)
        return 0

    if options.paths == [os.path.join(DEFAULT_GST_QA_ASSETS, "medias")]:
        if os.path.exists(DEFAULT_GST_QA_ASSETS):
            launch_command("cd %s && git pull --rebase" % DEFAULT_GST_QA_ASSETS)
        else:
            launch_command("git clone %s %s" % (DEFAULT_GST_QA_ASSETS_REPO, DEFAULT_GST_QA_ASSETS))

    tests_launcher.list_tests()
    tests_launcher.run_tests()
    tests_launcher.final_report()

    return 0
