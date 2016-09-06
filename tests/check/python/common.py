# -*- coding: utf-8 -*-
#
# Copyright (c) 2016 Alexandru Băluț <alexandru.balut@gmail.com>
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

from gi.repository import GES
from gi.repository import GLib
import tempfile


def create_main_loop():
    """Creates a MainLoop with a timeout."""
    mainloop = GLib.MainLoop()
    timed_out = False

    def quit_cb(unused):
        nonlocal timed_out
        timed_out = True
        mainloop.quit()

    def run(timeout_seconds=5):
        source = GLib.timeout_source_new_seconds(timeout_seconds)
        source.set_callback(quit_cb)
        source.attach()
        GLib.MainLoop.run(mainloop)
        source.destroy()
        if timed_out:
            raise Exception("Timed out after %s seconds" % timeout_seconds)

    mainloop.run = run
    return mainloop

def create_project(with_group=False, saved=False):
    """Creates a project with two clips in a group."""
    project = GES.Project()
    timeline = project.extract()
    layer = timeline.append_layer()

    if with_group:
        clip1 = GES.TitleClip()
        clip1.set_start(0)
        clip1.set_duration(10)
        layer.add_clip(clip1)
        clip2 = GES.TitleClip()
        clip2.set_start(100)
        clip2.set_duration(10)
        layer.add_clip(clip2)
        group = GES.Container.group([clip1, clip2])

    if saved:
        uri = "file://%s" % tempfile.NamedTemporaryFile(suffix=".xges").name
        project.save(timeline, uri, None, overwrite=True)

    return timeline

