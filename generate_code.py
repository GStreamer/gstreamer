#!/usr/bin/env python3

import argparse
import glob
import os
import re
import shutil
import subprocess


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--api-raw")
    parser.add_argument("--gapi-fixup")
    parser.add_argument("--metadata")
    parser.add_argument("--gapi-codegen")
    parser.add_argument("--glue-file", default="")
    parser.add_argument("--glue-includes", default="")
    parser.add_argument("--glue-libname", default="")
    parser.add_argument("--assembly-name")
    parser.add_argument("--extra-includes", action='append', default=[])
    parser.add_argument("--out")
    parser.add_argument("--files")
    parser.add_argument("--symbols")
    parser.add_argument("--schema")
    parser.add_argument("--fakeglue", action='store_true')

    opts = parser.parse_args()
    if opts.fakeglue:
        exit(0)

    if not opts.glue_libname:
        opts.glue_libname = opts.assembly_name + 'sharpglue-3'

    api_xml = os.path.join(opts.out, os.path.basename(
        opts.api_raw).replace('.raw', '.xml'))

    shutil.copyfile(opts.api_raw, api_xml)

    if shutil.which('mono'):
        launcher = ['mono']
    else:
        launcher = []

    cmd = [opts.gapi_fixup, "--api=" + api_xml]
    if opts.metadata:
        cmd += ["--metadata=" + opts.metadata]
    if opts.symbols:
        cmd.extend(['--symbols=' + opts.symbols])
    subprocess.check_call(launcher + cmd)

    cmd = [
        opts.gapi_codegen, '--generate', api_xml,
        '--outdir=' + opts.out,
        '--glue-filename=' + opts.glue_file,
        '--gluelib-name=' + opts.glue_libname,
        '--glue-includes=' + opts.glue_includes,
        '--assembly-name=' + opts.assembly_name,]

    if opts.schema:
        cmd += ['--schema=' + opts.schema]

    cmd += ['-I' + i for i in opts.extra_includes]

    subprocess.check_call(launcher + cmd)

    # WORKAROUND: Moving files into the out directory with special names
    # as meson doesn't like path separator in output names.
    regex = re.compile('_')
    dirs = set()
    for _f in opts.files.split(';'):
        fpath = os.path.join(opts.out, regex.sub("/", _f, 1))
        dirs.add(os.path.dirname(fpath))
        _f = os.path.join(opts.out, _f)
        shutil.move(fpath, _f)

    missing_files = []
    for _dir in dirs:
        missing_files.extend(glob.glob(os.path.join(_dir, '*.cs')))

    if missing_files:
        print("Following files were generated but not listed:\n    %s" %
              '\n    '.join(["'%s_%s'," % (m.split(os.path.sep)[-2], m.split(os.path.sep)[-1])
             for m in missing_files]))
        exit(1)

    for _dir in dirs:
        shutil.rmtree(_dir)
