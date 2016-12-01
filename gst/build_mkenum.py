#!/usr/bin/env python3

# This is in its own file rather than inside meson.build
# because a) mixing the two is ugly and b) trying to
# make special characters such as \n go through all
# backends is a fool's errand.

import sys, os, shutil, subprocess

h_array = ['--fhead',
           "#ifndef __GST_ENUM_TYPES_H__\n#define __GST_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n",
           '--fprod',
           "\n/* enumerations from \"@filename@\" */\n",
           '--vhead',
           "GType @enum_name@_get_type (void);\n#define GST_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n",
           '--ftail',
           "G_END_DECLS\n\n#endif /* __GST_ENUM_TYPES_H__ */"]

c_array = [
    '--fhead',
    "#include \"gst_private.h\"\n#include <gst/gst.h>\n#define C_ENUM(v) ((gint) v)\n#define C_FLAGS(v) ((guint) v)\n ",
    '--fprod',
    "\n/* enumerations from \"@filename@\" */",
    '--vhead',
    "GType\n@enum_name@_get_type (void)\n{\n  static gsize id = 0;\n  static const G@Type@Value values[] = {",
    '--vprod',
    "    { C_@TYPE@(@VALUENAME@), \"@VALUENAME@\", \"@valuenick@\" },",
    '--vtail',
    "    { 0, NULL, NULL }\n  };\n\n  if (g_once_init_enter (&id)) {\n    GType tmp = g_@type@_register_static (\"@EnumName@\", values);\n    g_once_init_leave (&id, tmp);\n  }\n\n  return (GType) id;\n}"
    ]

cmd = []
argn = 1
# Find the full command needed to run glib-mkenums
# On UNIX-like, this is just the full path to glib-mkenums
# On Windows, this is the full path to interpreter + full path to glib-mkenums
for arg in sys.argv[1:]:
    cmd.append(arg)
    argn += 1
    arg = arg.lower()
    if arg.endswith('glib-mkenums.pl') or arg.endswith('glib-mkenums'):
        break

# In Windows make sure to run through interpreter because there is no shebang
if sys.platform.startswith('win') and argn == 2:
    cmd = ['perl'] + cmd

ofilename = sys.argv[argn]
headers = sys.argv[argn + 1:]

if ofilename.endswith('.h'):
    arg_array = h_array
else:
    arg_array = c_array

pc = subprocess.Popen(cmd + arg_array + headers, stdout=subprocess.PIPE)
(stdo, _) = pc.communicate()
if pc.returncode != 0:
    sys.exit(pc.returncode)
open(ofilename, 'wb').write(stdo)
