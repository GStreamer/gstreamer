#!/usr/bin/env python3
# Verify that every GstPluginFeature subtype defined in core has its GType
# pre-registered (g_type_class_ref'd) during gst_init, and released again in
# gst_deinit.
#
# Why: registry chunks are deserialized with g_type_from_name(), which only
# resolves a type that is already registered in the process. The registry
# loader doesn't dlopen the plugin itself (unless the scanner fails), so
# a feature subtype that gst_init() forgets to register cannot be loaded from
# a cache, failing with "Unknown type from typename '...'". This is order- and
# plugin-set-dependent, hence flaky.
# See gstregistrychunks.c:gst_registry_chunks_load_feature().

import os
import re
import sys

# G_DEFINE_TYPE[_WITH_CODE|_WITH_PRIVATE] (GstFooFactory, gst_foo_factory,
#     GST_TYPE_PLUGIN_FEATURE, ...);
FEATURE_TYPE_RE = re.compile(
    r'G_DEFINE_(?:ABSTRACT_)?TYPE(?:_WITH_CODE|_WITH_PRIVATE)?\s*\(\s*'
    r'(\w+)\s*,\s*(\w+)\s*,\s*GST_TYPE_PLUGIN_FEATURE\b',
    re.MULTILINE)


def find_feature_get_types(gst_dir):
    """Return {get_type_symbol: TypeName} for concrete GstPluginFeature
    subclasses defined in core."""
    found = {}
    for name in sorted(os.listdir(gst_dir)):
        if not name.endswith('.c'):
            continue
        with open(os.path.join(gst_dir, name), encoding='utf-8') as f:
            text = f.read()
        for type_name, sym_prefix in FEATURE_TYPE_RE.findall(text):
            found[sym_prefix + '_get_type'] = type_name
    return found


def main():
    if len(sys.argv) != 2:
        print('usage: %s <gstreamer/gst dir>' % sys.argv[0], file=sys.stderr)
        return 2
    gst_dir = sys.argv[1]

    feature_types = find_feature_get_types(gst_dir)
    if not feature_types:
        print('FAIL: found no GstPluginFeature subtypes to check; the scan '
              'regex is probably out of date', file=sys.stderr)
        return 1

    with open(os.path.join(gst_dir, 'gst.c'), encoding='utf-8') as f:
        gst_c = f.read()

    # Collapse whitespace so multi-line calls match too.
    gst_c_flat = re.sub(r'\s+', ' ', gst_c)

    failures = []
    for sym, type_name in sorted(feature_types.items()):
        ref = 'g_type_class_ref (%s ())' % sym
        unref = 'g_type_class_unref (g_type_class_peek (%s ()))' % sym
        if ref not in gst_c_flat:
            failures.append(
                '%s (%s): missing "%s" in gst_init_check()/init_post; it '
                'cannot be deserialized from a registry cache' %
                (type_name, sym, ref))
        if unref not in gst_c_flat:
            failures.append(
                '%s (%s): missing "%s" in gst_deinit()' %
                (type_name, sym, unref))

    if failures:
        print('FAIL: GstPluginFeature subtypes not (un)registered by '
              'gst_init/gst_deinit:', file=sys.stderr)
        for msg in failures:
            print('  - ' + msg, file=sys.stderr)
        return 1

    print('OK: all %d core GstPluginFeature subtypes are pre-registered: %s' %
          (len(feature_types), ', '.join(sorted(feature_types.values()))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
