#!/usr/bin/env python3

import argparse
import os
from string import Template

TEMPLATE = Template('''
#include <gst/gst.h>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

$elements_declaration
$typefind_funcs_declaration
$device_providers_declaration
$dynamic_types_declaration
$plugins_declaration
$giomodules_declaration

 static
gboolean register_features_full (GstPlugin* plugin)
{
    $elements_registration
    $typefind_funcs_registration
    $device_providers_registration
    $dynamic_types_registration

    return TRUE;
}

_GST_EXPORT
void
gst_init_static_plugins (void)
{
  static gsize initialization_value = 0;
  if (g_once_init_enter (&initialization_value)) {

    gst_plugin_register_static (GST_VERSION_MAJOR, GST_VERSION_MINOR,
        GST_PLUGIN_FULL_FEATURES_NAME, "features linked into the gstreamer-full library", register_features_full, PACKAGE_VERSION, GST_FULL_LICENSE,
        "gstreamer-full", GETTEXT_PACKAGE, GST_PACKAGE_ORIGIN);

    $plugins_registration
    $giomodules_registration

    g_once_init_leave (&initialization_value, 1);
  }
}
''')


# Retrieve the plugin name as it can be a plugin filename
def get_plugin_name(name):
    for p in plugins:
        if name in p:
            return p
    return ''


def process_features(features_list, plugins, feature_prefix):
    plugins_list = plugins
    feature_declaration = []
    feature_registration = []
    if features_list is not None:
        feature_plugins = features_list.split(';')
        for plugin in feature_plugins:
            split = plugin.split(':')
            plugin_name = split[0].strip()
            if len(split) == 2:
                if (get_plugin_name(plugin_name)) != '':
                    plugins_list.remove(get_plugin_name(plugin_name))
                features = split[1].split(',')
                for feature in features:
                    feature = feature.replace("-", "_")
                    feature_declaration += ['%s_REGISTER_DECLARE(%s);' % (feature_prefix, feature)]
                    feature_registration += ['%s_REGISTER(%s, plugin);' % (feature_prefix, feature)]
    return (plugins_list, feature_declaration, feature_registration)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('-o', dest="output", help="Output file")
    parser.add_argument('-p', '--plugins', nargs='?', default='', dest="plugins", help="The list of plugins")
    parser.add_argument('-e', '--elements', nargs='?', default='', dest="elements", help="The list of plugin:elements")
    parser.add_argument('-t', '--type-finds', nargs='?', default='',
                        dest="typefindfuncs", help="The list of plugin:typefinds")
    parser.add_argument('-d', '--devide-providers', nargs='?', default='',
                        dest="deviceproviders", help="The list of plugin:deviceproviders")
    parser.add_argument('-T', '--dynamic-types', nargs='?', default='',
                        dest="dynamictypes", help="The list of plugin:dynamictypes")
    parser.add_argument('--giomodules', nargs='?', default='',
                        dest="giomodules", help="The list of GIO modules")
    options = parser.parse_args()
    if options.output is None:
        output_file = 'gstinitstaticplugins.c'
    else:
        output_file = options.output
    enable_staticelements_plugin = 0
    elements_declaration = []
    elements_registration = []
    typefind_funcs_declaration = []
    typefind_funcs_registration = []
    device_providers_declaration = []
    device_providers_registration = []
    dynamic_types_declaration = []
    dynamic_types_registration = []
    plugins_declaration = []
    plugins_registration = []
    giomodules_declaration = []
    giomodules_registration = []

    if ',' in options.plugins or ':' in options.plugins:
        print("Only ';' is allowed in the list of plugins.")
        exit(1)

    if options.plugins is None or options.plugins.isspace():
        plugins = []
    else:
        plugins = options.plugins.split(';')

    # process the features
    (plugins, elements_declaration, elements_registration) = process_features(options.elements, plugins, 'GST_ELEMENT')
    (plugins, typefind_funcs_declaration, typefind_funcs_registration) = process_features(
        options.typefindfuncs, plugins, 'GST_TYPE_FIND')
    (plugins, device_providers_declaration, device_providers_registration) = process_features(
        options.deviceproviders, plugins, 'GST_DEVICE_PROVIDER')
    (plugins, dynamic_types_declaration, dynamic_types_registration) = process_features(
        options.dynamictypes, plugins, 'GST_DYNAMIC_TYPE')

    # Enable plugin or elements according to the ';' separated list.
    for plugin in plugins:
        split = plugin.split(':')
        plugin_name = split[0]
        if plugin_name == '':
            continue
        filename = os.path.basename(plugin).strip()
        if filename.startswith('libgst') and filename.endswith('.a'):
            plugin_name = filename[len('libgst'):-len('.a')]
        elif filename.startswith('libgst') and filename.endswith('.lib'):
            plugin_name = filename[len('libgst'):-len('.lib')]
        plugins_registration += ['GST_PLUGIN_STATIC_REGISTER(%s);' % (plugin_name)]
        plugins_declaration += ['GST_PLUGIN_STATIC_DECLARE(%s);' % (plugin_name)]

    giomodules = options.giomodules.split(';') if options.giomodules else []
    for module_name in giomodules:
        if module_name.startswith('gio'):
            module_name = module_name[3:]
        giomodules_declaration.append(f'extern void g_io_{module_name}_load (gpointer data);')
        giomodules_registration.append(f'g_io_{module_name}_load (NULL);')

    with open(output_file.strip(), "w") as f:
        static_elements_plugin = ''
        f.write(TEMPLATE.substitute({
            'elements_declaration': '\n'.join(elements_declaration),
            'elements_registration': '\n    '.join(elements_registration),
            'typefind_funcs_declaration': '\n'.join(typefind_funcs_declaration),
            'typefind_funcs_registration': '\n    '.join(typefind_funcs_registration),
            'device_providers_declaration': '\n'.join(device_providers_declaration),
            'device_providers_registration': '\n    '.join(device_providers_registration),
            'dynamic_types_declaration': '\n'.join(dynamic_types_declaration),
            'dynamic_types_registration': '\n    '.join(dynamic_types_registration),
            'plugins_declaration': '\n'.join(plugins_declaration),
            'plugins_registration': '\n    '.join(plugins_registration),
            'giomodules_declaration': '\n'.join(giomodules_declaration),
            'giomodules_registration': '\n    '.join(giomodules_registration),
        }))
