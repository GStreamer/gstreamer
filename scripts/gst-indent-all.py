#!/usr/bin/env python

from itertools import filterfalse
import os
import re
import subprocess
from gst_indent_common import indent

def readfile(f):
    if os.path.exists(f):
        expressions = open(f, 'r', encoding='utf-8').read().splitlines()
        expressions = [re.compile(i) for i in expressions]
        return lambda x: any(i.match(x) for i in expressions)
    else:
        return None


def listfiles(single_glob):
    if os.environ.get("CI_PROJECT_NAME"):
        return subprocess.check_output(['git', 'ls-files', single_glob],
                                       universal_newlines=True).splitlines()
    else:
        return subprocess.check_output(['git', 'diff-index', '--cached', '--name-only', 'HEAD', '--diff-filter=ACMR', single_glob],
                                       universal_newlines=True).splitlines()


if __name__ == '__main__':
    basedir = os.path.dirname(__file__)

    filter_in_c = readfile('.indentignore')
    listing = listfiles('*.c')
    if filter_in_c:
        listing = filterfalse(filter_in_c, listing)

    for entry in listing:
        indent(entry)

    filter_in_cpp = readfile('.indent_cpp_list')
    listing = listfiles('*.cpp')
    if filter_in_cpp:
        listing = filter(filter_in_cpp, listing)

    for entry in listing:
        indent(entry)
