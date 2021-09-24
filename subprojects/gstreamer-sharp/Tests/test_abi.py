#!/usr/bin/env python3

import difflib
import sys
import shutil
import subprocess

reference_abi = subprocess.check_output(sys.argv[1]).decode().split("\n")
launcher = []
if shutil.which("mono"):
    launcher = ["mono", "--debug"]
csharp_abi = subprocess.check_output(launcher + [sys.argv[2]]).decode().split("\n")
print("Comparing output of %s and %s" % (sys.argv[1], sys.argv[2]))

res = 0
for line in difflib.unified_diff(reference_abi, csharp_abi):
    res = 1
    print(line)

if res:
    files = [(sys.argv[1] + ".res", reference_abi),
             (sys.argv[2] + 'res', csharp_abi)]

    for f, vals in files:
        with open(f, "w") as _f:
            print("Outputing results in " + f)
            _f.write("\n".join(vals))
sys.exit(res)
