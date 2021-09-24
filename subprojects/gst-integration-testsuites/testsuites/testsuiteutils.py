# -*- Mode: Python -*- vi:si:et:sw=4:sts=4:ts=4:syntax=python
#
# Copyright (c) 2015, Thibault Saunier <thibault.saunier@collabora.com>
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


import json
import os
import subprocess
import sys

from urllib.request import urlretrieve
from urllib.parse import quote

try:
    from launcher.config import GST_VALIDATE_TESTSUITE_VERSION
except ImportError:
    GST_VALIDATE_TESTSUITE_VERSION = "master"


last_message_length = 0

os.environ["GST_VALIDATE_CONFIG"] = os.path.abspath(
    os.path.join(
        os.path.dirname(__file__), "..", "integration-testsuites.config")) + os.pathsep + os.environ.get("GST_VALIDATE_CONFIG", "")


def message(string):
    if sys.stdout.isatty():
        global last_message_length
        print('\r' + string + ' ' * max(0, last_message_length - len(string)), end='')
        last_message_length = len(string)
    else:
        print(string)


def sizeof_fmt(num, suffix='B'):
    for unit in ['', 'Ki', 'Mi', 'Gi', 'Ti', 'Pi', 'Ei', 'Zi']:
        if abs(num) < 1024.0:
            return "%3.1f%s%s" % (num, unit, suffix)
        num /= 1024.0
    return "%.1f%s%s" % (num, 'Yi', suffix)


URL = ""


def reporthook(blocknum, blocksize, totalsize):
    global URL
    readsofar = blocknum * blocksize
    if totalsize > 0:
        percent = readsofar * 1e2 / totalsize
        s = "\r%s —%5.1f%% %s / %s" % (URL,
                                       percent, sizeof_fmt(readsofar), sizeof_fmt(totalsize)) \
            + ' ' * 50
        message(s)
    else:  # total size is unknown
        message("read %d" % (readsofar,))


def download_files(assets_dir):
    print("Downloading %s" % assets_dir if assets_dir else "all assets")
    fdir = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                        '..', 'medias'))

    with open(os.path.join(fdir, 'files.json'), 'r') as f:
        files = json.load(f)

    for f, ref_filesize in files:
        if assets_dir and not f.startswith(assets_dir):
            continue

        fname = os.path.join(fdir, f)
        if os.path.exists(fname) and os.path.getsize(fname) == ref_filesize:
            message('%s... OK' % fname)
            continue

        os.makedirs(os.path.dirname(fname), exist_ok=True)
        rpath = fname[len(fdir) + 1:]
        global URL
        URL = 'https://gstreamer.freedesktop.org/data/media/gst-integration-testsuite/' + \
            quote(rpath)
        if sys.stdout.isatty():
            message("\rDownloading %s" % URL)
            hook = reporthook
        else:
            message("Downloading %s" % URL)
            hook = None
        try:
            urlretrieve(URL, fname, hook)
        except BaseException:
            print("\nCould not retieved %s" % URL)
            raise

        if os.path.getsize(fname) != ref_filesize:
            print("ERROR: File %s expected size %s != %s, this should never happen!",
                  fname, os.path.getsize(fname), ref_filesize)
            exit(1)
    print("")


def update_assets(options, assets_dir):
    try:
        if options.sync_version is not None:
            sync_version = options.sync_version
        else:
            sync_version = GST_VALIDATE_TESTSUITE_VERSION
        CHECKOUT_BRANCH_COMMAND = "git fetch origin && (git checkout origin/%s || git checkout tags/%s)" % (
            sync_version, sync_version)
        if options.force_sync:
            subprocess.check_call(["git", "reset", "--hard"], cwd=assets_dir)
        print("Checking out %s" % sync_version)
        subprocess.check_call(CHECKOUT_BRANCH_COMMAND, shell=True, cwd=assets_dir)
        download_files(os.path.basename(os.path.join(assets_dir)))
    except Exception as e:
        print("\nERROR: Could not update assets \n\n: %s" % e)

        return False

    return True
