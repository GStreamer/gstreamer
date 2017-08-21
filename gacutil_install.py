#!/usr/bin/env python3

import os
import sys
import subprocess

outdir = os.path.join(os.environ['MESON_INSTALL_DESTDIR_PREFIX'], 'lib')
builddir = os.environ['MESON_BUILD_ROOT']

for i in range(1, len(sys.argv), 2):
    assembly_name, fname = sys.argv[i], os.path.join(builddir, sys.argv[i + 1])

    cmd = ['gacutil', '/i', fname, '/f', '/package', assembly_name, '/root', outdir]
    print('(%s) Running %s' % (os.path.abspath(os.path.curdir), ' '.join(cmd)))
    subprocess.check_call(cmd)
