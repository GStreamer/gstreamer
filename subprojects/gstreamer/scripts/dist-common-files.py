#!/usr/bin/env python3
#
# Copyright (C) 2023-2026 Tim-Philipp Müller <tim centricular net>
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
import sys

if __name__ == "__main__":
    dist_root = os.environ['MESON_DIST_ROOT']
    build_root = os.environ['MESON_BUILD_ROOT']
    source_root = os.environ['MESON_SOURCE_ROOT']
    project_version = sys.argv[1]
    pwd = os.environ['PWD']
    tmpdir = tempfile.gettempdir()

    ver_array = project_version.split('.')
    major_minor = '{}.{}'.format(ver_array[0], ver_array[1])

    module = os.path.basename(os.path.normpath(source_root))

    print('Copying README.md into dist staging directory ..')
    readme_src = os.path.join(source_root, '..', '..', 'README.md')
    shutil.copy2(readme_src, dist_root)

    # Release notes (instead of NEWS) - could also write it out as NEWS.md
    print('Copying release notes into dist staging directory ..')
    relnotes_src = os.path.join(
        source_root, '..', '..', 'release-notes', major_minor, f'release-notes-{major_minor}.md')
    with open(relnotes_src, 'r') as f:
        lines = f.readlines()
    if not f'### {project_version}\n' in lines:
        sys.exit(
            f'Update {relnotes_src} first, must contain a section for {project_version}')
    if not project_version.endswith('.0'):
        found = False
        for line in lines:
            if line.startswith('The latest') and project_version in line:
                found = True
        if not found:
            sys.exit(
                f'Update {relnotes_src} first, header should say latest version is {project_version}.')
    shutil.copy2(relnotes_src, dist_root)

    # RELEASE
    print('Copying RELEASE into dist staging directory ..')
    rel_src = os.path.join(source_root, '..', '..', 'release-notes',
                           major_minor, f'RELEASE-{major_minor}.template')
    with open(rel_src, 'r') as f:
        lines = f.readlines()

    assert (lines[0].startswith('This is GStreamer'))

    if module == 'gstreamer':
        lines[0] = f'This is GStreamer core {project_version}\n'
    else:
        lines[0] = f'This is GStreamer {module} {project_version}\n'

    with open(os.path.join(dist_root, 'RELEASE'), 'w') as f:
        f.writelines(lines)
