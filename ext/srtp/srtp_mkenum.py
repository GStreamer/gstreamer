#!/usr/bin/env python3

# This is in its own file rather than inside meson.build
# because a) mixing the two is ugly and b) trying to
# make special characters such as \n go through all
# backends is a fool's errand.

import sys, os, shutil, subprocess

h_array = ['--fhead',
           "#ifndef __GST_SRTP_ENUM_TYPES_H__\n#define __GST_SRTP_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n",
           '--fprod',
           "\n/* enumerations from \"@filename@\" */\n",
           '--vhead',
           'GType @enum_name@_get_type (void);\n#define GST_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n',
           '--ftail',
           'G_END_DECLS\n\n#endif /* __GST_SRTP_ENUM_TYPES_H__ */',
           ]

c_array = ['--fhead',
           "#include \"gstsrtp-enumtypes.h\"\n\n#include \"gstsrtpenums.h\"",
           '--fprod',
           "\n/* enumerations from \"@filename@\" */",
           '--vhead',
           "GType\n@enum_name@_get_type (void)\n{\n  static volatile gsize g_define_type_id__volatile = 0;\n  if (g_once_init_enter (&g_define_type_id__volatile)) {\n    static const G@Type@Value values[] = {",
           '--vprod',
           "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" },",
           '--vtail',
           "      { 0, NULL, NULL }\n    };\n    GType g_define_type_id = g_@type@_register_static (\"@EnumName@\", values);\n    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);\n  }\n  return g_define_type_id__volatile;\n}\n",
           ]

cmd = []
argn = 1
# Find the full command needed to run glib-mkenums
# On UNIX-like, this is just the full path to glib-mkenums
# On Windows, this is the full path to interpreter + full path to glib-mkenums
for arg in sys.argv[1:]:
    cmd.append(arg)
    argn += 1
    if arg.endswith('glib-mkenums'):
        break
ofilename = sys.argv[argn]
headers = sys.argv[argn + 1:]

if ofilename.endswith('.h'):
    arg_array = h_array
else:
    arg_array = c_array

cmd_array = cmd + arg_array + headers
pc = subprocess.Popen(cmd_array, stdout=subprocess.PIPE)
(stdo, _) = pc.communicate()
if pc.returncode != 0:
    sys.exit(pc.returncode)
open(ofilename, 'wb').write(stdo)
