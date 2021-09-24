# Pastebin PJeVxRyU
# -*- Mode: Python -*- vi:si:et:sw=4:sts=4:ts=4:syntax=python
#
# Copyright (c) 2014 Thibault Saunier <thibault.saunier@collabora.com>
# Copyright (c) 2017 Sebastian Droege <sebastian@centricular.com>
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

"""
The GstValidate DASH-IF test-vectors testsuite
"""

import json
import requests
import sys
import xml.etree.ElementTree as ET

TESTVECTOR_URL = "http://testassets.dashif.org:3000/v1/testvectors"


def cleanup_entries(data):
    # we need to do some minor cleanup of the entries:
    # * Replace <a href=..>something</a>
    # * Add __ID corresponding to the testvector unique key on the website
    d = data["data"]
    for entry in d:
        # "testvector": "<a href=\"#testvector/details/58a5e0707459f8cb201b8ceb\">W3C Clear Key - audio only variant</a>",
        testvector = entry["testvector"]
        tmp = ET.XML(testvector)
        _id = tmp.attrib["href"].split('/')[-1]
        _desc = tmp.text
        # use a key that is almost certain to be the first one. Ensures entries are sorted by ID
        entry["A__ID"] = _id
        entry["testvector_description"] = _desc

        # "url": "<a href=https://media.axprod.net/TestVectors/v7-MultiDRM-MultiKey-MultiPeriod/Manifest_1080p_ClearKey.mpd>Link</a>"
        # badly formed html, let's just use splitters
        url = entry["url"]
        _url = url.split("<a href=")[1].split(">Link<")[0]
        entry["url"] = _url


def update_testvector_list(outputfile=None):
    # download and cleanup testvector json
    resp = requests.get(url=TESTVECTOR_URL)
    data = json.loads(resp.text)
    cleanup_entries(data)
    if outputfile is None:
        print(json.dumps(data["data"], indent=4, sort_keys=True))
    else:
        json.dump(data["data"], fp=open(outputfile, "w"), indent=4, sort_keys=True)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        update_testvector_list(sys.argv[1])
    else:
        update_testvector_list()
