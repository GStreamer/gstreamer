#!/usr/bin/python
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
""" Some utilies. """

import os


class Result(object):
    NOT_RUN = "Not run"
    FAILED = "Failed"
    TIMEOUT = "Timeout"
    PASSED = "Passed"


class Colors(object):
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'


def desactivate_colors(self):
    Colors.HEADER = ''
    Colors.OKBLUE = ''
    Colors.OKGREEN = ''
    Colors.WARNING = ''
    Colors.FAIL = ''
    Colors.ENDC = ''


def mkdir(directory):
    try:
        os.makedirs(directory)
    except os.error:
        pass


def printc (message, color="", title=False):
    if title:
        message = len(message) * '=' + message + len(message) * '='
    if hasattr(message, "result") and color == '':
        if message.result == Result.FAILED:
            color = Colors.FAIL
        elif message.result == Result.PASSED:
            color = Colors.OKGREEN
        else:
            color = Colors.OKBLUE

    print color + str(message) + Colors.ENDC


def launch_command(command, color=None):
    printc(command, Colors.OKGREEN, True)
    os.system(command)
