#!/usr/bin/env python2
#
# Copyright (c) 2013,Thibault Saunier <thibault.saunier@collabora.com>
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

""" Test Reporters implementation. """

import os
import re
import sys
import time
import codecs
import datetime
from loggable import Loggable
from xml.sax import saxutils
from utils import mkdir, Result, printc, Colors

UNICODE_STRINGS = (type(unicode()) == type(str()))


class UnknownResult(Exception):
    pass


CONTROL_CHARACTERS = re.compile(r"[\000-\010\013\014\016-\037]")


def xml_safe(value):
    """Replaces invalid XML characters with '?'."""
    return CONTROL_CHARACTERS.sub('?', value)


def escape_cdata(cdata):
    """Escape a string for an XML CDATA section."""
    return xml_safe(cdata).replace(']]>', ']]>]]&gt;<![CDATA[')


class Reporter(Loggable):
    name = 'simple'

    def __init__(self, options):
        Loggable.__init__(self)

        self.out = None
        self.options = options
        self._start_time = 0
        self.stats = {'timeout': 0,
                      'failures': 0,
                      'passed': 0,
                      'skipped': 0
                      }
        self.results = []

    def init_timer(self):
        """Initialize a timer before starting tests."""
        self._start_time = time.time()

    def before_test(self, test):
        path = os.path.join(self.options.logsdir,
                            test.classname.replace(".", os.sep))
        mkdir(os.path.dirname(path))
        test.logfile = path

        if self.options.redirect_logs == 'stdout':
            self.out = sys.stdout
        elif self.options.redirect_logs == 'stderr':
            self.out = sys.stderr
        else:
            self.out = open(path, 'w+')

    def set_failed(self, test):
        self.stats["failure"] += 1

    def set_passed(self, test):
        self.stats["passed"] += 1

    def add_results(self, test):
        self.debug("%s", test)
        if test.result == Result.PASSED:
            self.set_passed(test)
        elif test.result == Result.FAILED or \
                test.result == Result.TIMEOUT:
            self.set_failed(test)
        else:
            raise UnknownResult("%s" % test.result)

    def after_test(self, test):
        if test not in self.results:
            self.results.append(test)

        self.add_results(test)
        if not self.options.redirect_logs:
            self.out.close()

        self.out = None

    def final_report(self):
        print "\n"
        printc("Final Report:", title=True)
        for test in sorted(self.results, key=lambda test: test.result):
            printc(test)
            if test.result != Result.PASSED:
                print "\n"

        print "\n"
        lenstat = (len("Statistics") + 1)
        printc("Statistics:\n%s" % (lenstat * "-"), Colors.OKBLUE)
        printc("\n%sTotal time spent: %s seconds\n" %
               ((lenstat * " "), datetime.timedelta(
                   seconds=(time.time() - self._start_time))),
               Colors.OKBLUE)
        printc("%sPassed: %d" %
               (lenstat * " ", self.stats["passed"]), Colors.OKGREEN)
        printc("%sFailed: %d" %
               (lenstat * " ", self.stats["failures"]), Colors.FAIL)
        printc("%s%s" %
               (lenstat * " ", (len("Failed: 0")) * "-"), Colors.OKBLUE)

        total = self.stats["failures"] + self.stats["passed"]
        color = Colors.WARNING
        if total == self.stats["passed"]:
            color = Colors.OKGREEN
        elif total == self.stats["failures"]:
            color = Colors.FAIL

        printc("%sTotal: %d" % (lenstat * " ", total), color)


class XunitReporter(Reporter):

    """This reporter provides test results in the standard XUnit XML format."""
    name = 'xunit'
    encoding = 'UTF-8'
    xml_file = None

    def __init__(self, options):
        super(XunitReporter, self).__init__(options)
        self.errorlist = []

    def final_report(self):
        self.report()
        super(XunitReporter, self).final_report()

    def _get_captured(self, test):
        captured = ""
        if self.out and not self.options.redirect_logs:
            self.out.seek(0)
            value = self.out.read()
            if value:
                captured += '<system-out><![CDATA[%s' % \
                    escape_cdata(value)
            for extralog in test.extra_logfiles:
                captured += "\n\n===== %s =====\n\n" % escape_cdata(
                    os.path.basename(extralog))
                value = test.get_extra_log_content(extralog)
                captured += escape_cdata(value)

            captured += "]]></system-out>"

        return captured

    def _quoteattr(self, attr):
        """Escape an XML attribute. Value can be unicode."""
        attr = xml_safe(attr)
        if isinstance(attr, unicode) and not UNICODE_STRINGS:
            attr = attr.encode(self.encoding)
        return saxutils.quoteattr(attr)

    def report(self):
        """Writes an Xunit-formatted XML file

        The file includes a report of test errors and failures.

        """
        self.debug("Writing XML file to: %s", self.options.xunit_file)
        self.xml_file = codecs.open(self.options.xunit_file, 'w',
                                    self.encoding, 'replace')
        self.stats['encoding'] = self.encoding
        self.stats['total'] = (self.stats['timeout'] + self.stats['failures']
                               + self.stats['passed'] + self.stats['skipped'])
        self.xml_file.write(u'<?xml version="1.0" encoding="%(encoding)s"?>'
                            u'<testsuite name="gst-validate-launcher" tests="%(total)d" '
                            u'errors="%(timeout)d" failures="%(failures)d" '
                            u'skip="%(skipped)d">' % self.stats)
        self.xml_file.write(u''.join([self._forceUnicode(e)
                            for e in self.errorlist]))
        self.xml_file.write(u'</testsuite>')
        self.xml_file.close()

    def set_failed(self, test):
        """Add failure output to Xunit report.
        """
        self.stats['failures'] += 1
        self.errorlist.append(
            '<testcase classname=%(cls)s name=%(name)s time="%(taken).3f">'
            '<failure type=%(errtype)s message=%(message)s>'
            '</failure>%(systemout)s</testcase>' %
            {'cls': self._quoteattr(test.get_classname()),
             'name': self._quoteattr(test.get_name()),
             'taken': test.time_taken,
             'errtype': self._quoteattr(test.result),
             'message': self._quoteattr(test.message),
             'systemout': self._get_captured(test),
             })

    def set_passed(self, test):
        """Add success output to Xunit report.
        """
        self.stats['passed'] += 1
        self.errorlist.append(
            '<testcase classname=%(cls)s name=%(name)s '
            'time="%(taken).3f">%(systemout)s</testcase>' %
            {'cls': self._quoteattr(test.get_classname()),
             'name': self._quoteattr(test.get_name()),
             'taken': test.time_taken,
             'systemout': self._get_captured(test),
             })

    def _forceUnicode(self, s):
        if not UNICODE_STRINGS:
            if isinstance(s, str):
                s = s.decode(self.encoding, 'replace')
        return s
