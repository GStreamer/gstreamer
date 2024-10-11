#!/usr/bin/env python3
# GStreamer
# Copyright (C) 2024 Michael Tretter <m.tretter@pengutronix.de>
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

import shutil
import subprocess
import sys

assert (len(sys.argv) == 5)

qsb_tool = sys.argv[1]
qsb_output = sys.argv[2]
gles_shader = sys.argv[3]
qsb_input = sys.argv[4]

# Copy the qsb file since the qsb tool replaces the shader in place
shutil.copyfile(qsb_input, qsb_output)

subprocess.run([qsb_tool,
                '--silent',
                '--replace', 'glsl,100es,{}'.format(gles_shader),
                qsb_output])
