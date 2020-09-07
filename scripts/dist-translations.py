#!/usr/bin/env python3
#
# Copyright (C) 2020 Tim-Philipp Müller <tim centricular net>
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
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

import os
import subprocess
import shutil
import tempfile

if __name__ == "__main__":
    dist_root = os.environ['MESON_DIST_ROOT']
    build_root = os.environ['MESON_BUILD_ROOT']
    source_root = os.environ['MESON_SOURCE_ROOT']
    pwd = os.environ['PWD']
    tmpdir = tempfile.gettempdir()

    module = os.path.basename(os.path.normpath(source_root))

    # Generate pot file
    print('Generating pot file ...')
    subprocess.run(['ninja', '-C', build_root, module + '-1.0-pot'], check=True)

    # Dist pot file in tarball
    print('Copying pot file into dist staging directory ...')
    pot_src = os.path.join(source_root, 'po', module + '-1.0.pot')
    dist_po_dir = os.path.join(dist_root, 'po')
    shutil.copy2(pot_src, dist_po_dir)
