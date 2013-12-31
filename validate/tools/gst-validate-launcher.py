#!/usr//bin/python
import os
from testdefinitions import _TestsLauncher
from optparse import OptionParser

def main():
    parser = OptionParser()
    parser.add_option("-g", "--gdb", dest="gdb",
                      action="store_true",
                      default=False,
                      help="Run applications into gdb")
    parser.add_option("-f", "--forever", dest="forever",
                      action="store_true", default=False,
                      help="Keep running tests until one fails")
    parser.add_option("-F", "--fatal-error", dest="fatal_error",
                      action="store_true", default=False,
                      help="Stop on first fail")
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

    tests_launcher = _TestsLauncher()
    tests_launcher.add_options(parser)
    (options, args) = parser.parse_args()
    if options.xunit_file is None:
        options.xunit_file = os.path.join(options.logsdir, "xunit.xml")
    tests_launcher.set_settings(options, args)
    tests_launcher.list_tests()
    if options.list_tests:
        for test in tests_launcher.tests:
            print test
        return 0
    tests_launcher.run_tests()
    tests_launcher.final_report()
    return 0

if "__main__" == __name__:
    exit(main())
