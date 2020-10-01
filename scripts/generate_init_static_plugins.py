#!/usr/bin/env python3

import argparse
import os
from string import Template

TEMPLATE = Template('''
#include <gst/gst.h>

$plugins_declaration

void
gst_init_static_plugins (void)
{
  static gsize initialization_value = 0;
  if (g_once_init_enter (&initialization_value)) {
    $plugins_registration
    g_once_init_leave (&initialization_value, 1);
  }
}
''')

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(dest="output", help="Output file")
    parser.add_argument(dest="plugins", help="The list of plugins")

    options = parser.parse_args()

    names = set()
    for plugin in options.plugins.split(os.pathsep):
        filename = os.path.basename(plugin)
        if filename.startswith('libgst') and filename.endswith('.a'):
            names.add(filename[len('libgst'):-len('.a')])

    registration = ['GST_PLUGIN_STATIC_REGISTER(%s);' % name for name in names]
    declaration = ['GST_PLUGIN_STATIC_DECLARE(%s);' % name for name in names]

    with open(options.output, "w") as f:
        f.write(TEMPLATE.substitute({
            'plugins_declaration': '\n'.join(declaration),
            'plugins_registration': '\n  '.join(registration),
            }))
