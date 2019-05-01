#!/usr/bin/env python3

import re
import sys
import subprocess

flex = sys.argv[1]

out = subprocess.check_output([flex, '--version'], universal_newlines=True,
                              stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
print(re.search(r'(\d+\.\d+(\.\d+)?)', out).group())
