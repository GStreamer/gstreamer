#!/usr/bin/env python
#
# gst-python
# Copyright (C) 2002 David I. Lehn
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.
# 
# Author: David I. Lehn <dlehn@users.sourceforge.net>
#

import sys
import gst
import time
from identity import Identity

def update(sender, *args):
    print sender.get_name(), args

def build(filters, b):
    # create a new bin to hold the elements
    bin = gst.Pipeline('pipeline')

    src = gst.Element('fakesrc', 'source');
    src.set_property('silent', 1)
    src.set_property('num_buffers', b)

    sink = gst.Element('fakesink', 'sink')
    sink.set_property('silent', 1)

    elements = [src] + filters + [sink]
    bin.add_many(*elements)
    gst.element_link_many(*elements)
    return bin

def filter(bin):
    bin.set_state(gst.STATE_PLAYING);
    while bin.iterate():
        pass
    bin.set_state(gst.STATE_NULL)

ccnt = 0
def c():
    global ccnt
    id = gst.Element('identity', 'c identity %d' % ccnt);
    id.set_property('silent', 1)
    id.set_property('loop_based', 0)
    ccnt += 1
    return id

pcnt = 0
def py():
    id = Identity()
    assert id
    global pcnt
    id.set_name('py identity %d' % pcnt)
    pcnt += 1
    return id

def check(f, n, b):
    fs = []
    for i in range(n):
        fs.append(f())

    pipe = build(fs, b)

    start = time.time()
    ret = filter(pipe)
    end = time.time()
    print '%s b:%d i:%d t:%f' % (f, b, n, end - start)
    return ret

def main(args):
    "Identity timer and latency check"

    if len(args) < 3:
        print 'usage: %s identites buffers' % args[0]
        return -1
    n = int(args[1])
    b = int(args[2])
    
    for f in (c, py):
        check(f, n, b)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
