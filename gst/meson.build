gst_sources = [
  'gst.c',
  'gstobject.c',
  'gstallocator.c',
  'gstbin.c',
  'gstbuffer.c',
  'gstbufferlist.c',
  'gstbufferpool.c',
  'gstbus.c',
  'gstcaps.c',
  'gstcapsfeatures.c',
  'gstchildproxy.c',
  'gstclock.c',
  'gstcontext.c',
  'gstcontrolbinding.c',
  'gstcontrolsource.c',
  'gstdatetime.c',
  'gstdebugutils.c',
  'gstdevice.c',
  'gstdeviceprovider.c',
  'gstdeviceproviderfactory.c',
  'gstdynamictypefactory.c',
  'gstelement.c',
  'gstelementfactory.c',
  'gsterror.c',
  'gstevent.c',
  'gstformat.c',
  'gstghostpad.c',
  'gstdevicemonitor.c',
  'gstinfo.c',
  'gstiterator.c',
  'gstatomicqueue.c',
  'gstmessage.c',
  'gstmeta.c',
  'gstmemory.c',
  'gstminiobject.c',
  'gstpad.c',
  'gstpadtemplate.c',
  'gstparamspecs.c',
  'gstpipeline.c',
  'gstplugin.c',
  'gstpluginfeature.c',
  'gstpluginloader.c',
  'gstpoll.c',
  'gstpreset.c',
  'gstprotection.c',
  'gstquark.c',
  'gstquery.c',
  'gstregistry.c',
  'gstregistrychunks.c',
  'gstpromise.c',
  'gstsample.c',
  'gstsegment.c',
  'gststreamcollection.c',
  'gststreams.c',
  'gststructure.c',
  'gstsystemclock.c',
  'gsttaglist.c',
  'gsttagsetter.c',
  'gsttask.c',
  'gsttaskpool.c',
  'gsttoc.c',
  'gsttocsetter.c',
  'gsttracer.c',
  'gsttracerfactory.c',
  'gsttracerrecord.c',
  'gsttracerutils.c',
  'gsttypefind.c',
  'gsttypefindfactory.c',
  'gsturi.c',
  'gstutils.c',
  'gstvalue.c',
  'gstparse.c',
]

gst_headers = [
  'gst.h',
  'glib-compat.h',
  'gstobject.h',
  'gstallocator.h',
  'gstbin.h',
  'gstbuffer.h',
  'gstbufferlist.h',
  'gstbufferpool.h',
  'gstbus.h',
  'gstcaps.h',
  'gstcapsfeatures.h',
  'gstchildproxy.h',
  'gstclock.h',
  'gstcompat.h',
  'gstcontext.h',
  'gstcontrolbinding.h',
  'gstcontrolsource.h',
  'gstdatetime.h',
  'gstdebugutils.h',
  'gstelement.h',
  'gstelementmetadata.h',
  'gstdevice.h',
  'gstdeviceprovider.h',
  'gstdeviceproviderfactory.h',
  'gstdynamictypefactory.h',
  'gstelementfactory.h',
  'gsterror.h',
  'gstevent.h',
  'gstformat.h',
  'gstghostpad.h',
  'gstdevicemonitor.h',
  'gstinfo.h',
  'gstiterator.h',
  'gstatomicqueue.h',
  'gstmacros.h',
  'gstmessage.h',
  'gstmeta.h',
  'gstmemory.h',
  'gstminiobject.h',
  'gstpad.h',
  'gstpadtemplate.h',
  'gstparamspecs.h',
  'gstpipeline.h',
  'gstplugin.h',
  'gstpluginfeature.h',
  'gstpoll.h',
  'gstpreset.h',
  'gstprotection.h',
  'gstquery.h',
  'gstpromise.h',
  'gstsample.h',
  'gstsegment.h',
  'gststreamcollection.h',
  'gststreams.h',
  'gststructure.h',
  'gstsystemclock.h',
  'gsttaglist.h',
  'gsttagsetter.h',
  'gsttask.h',
  'gsttaskpool.h',
  'gsttoc.h',
  'gsttocsetter.h',
  'gsttracer.h',
  'gsttracerfactory.h',
  'gsttracerrecord.h',
  'gsttypefind.h',
  'gsttypefindfactory.h',
  'gsturi.h',
  'gstutils.h',
  'gstvalue.h',
  'gstregistry.h',
  'gstparse.h',
  'math-compat.h',
]
install_headers(gst_headers, subdir : 'gstreamer-1.0/gst')

gst_registry = get_option('registry')
if gst_registry
  gst_registry_sources = ['gstregistrybinary.c']
else
  gst_registry_sources = []
endif

# Make copy so configure_file consumes the copy and we can
# still add to the original cdata later.
gst_cdata = cdata

if gst_debug
  gst_cdata.set('GST_DISABLE_GST_DEBUG_DEFINE', '#undef GST_DISABLE_GST_DEBUG')
else
  gst_cdata.set('GST_DISABLE_GST_DEBUG_DEFINE', '#define GST_DISABLE_GST_DEBUG 1')
endif

if gst_registry
  gst_cdata.set('GST_DISABLE_REGISTRY_DEFINE', '#undef GST_DISABLE_REGISTRY')
else
  gst_cdata.set('GST_DISABLE_REGISTRY_DEFINE', '#define GST_DISABLE_REGISTRY 1')
endif

gst_parse = get_option('gst_parse')
if gst_parse
  gst_cdata.set('GST_DISABLE_PARSE_DEFINE', '#undef GST_DISABLE_PARSE')
else
  gst_cdata.set('GST_DISABLE_PARSE_DEFINE', '#define GST_DISABLE_PARSE 1')
endif

gst_cdata.set10('GST_DISABLE_CAST_CHECKS_DEFINE', cast_checks.disabled())
gst_cdata.set10('GST_DISABLE_GLIB_ASSERTS_DEFINE', glib_asserts.disabled())
gst_cdata.set10('GST_DISABLE_GLIB_CHECKS_DEFINE', glib_checks.disabled())

# FIXME: add --disable-plugin option?
gst_cdata.set('GST_DISABLE_PLUGIN_DEFINE', '#undef GST_DISABLE_PLUGIN')

configure_file(input : 'gstconfig.h.in',
  output : 'gstconfig.h',
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/gst'),
  configuration : gst_cdata)

version_cdata = configuration_data()
version_cdata.set('GST_VERSION_MAJOR', gst_version_major)
version_cdata.set('GST_VERSION_MINOR', gst_version_minor)
version_cdata.set('GST_VERSION_MICRO', gst_version_micro)
version_cdata.set('GST_VERSION_NANO', gst_version_nano)

gst_version_h = configure_file(input : 'gstversion.h.in',
  output : 'gstversion.h',
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/gst'),
  configuration : version_cdata)

gst_enums = gnome.mkenums_simple('gstenumtypes',
  sources : gst_headers,
  header_prefix : '#include <gst/gstconfig.h>',
  body_prefix : '#include "gst/gst_private.h"\n#include <gst/gst.h>',
  decorator : 'GST_API',
  install_header : true,
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/gst'))

gstenum_h = gst_enums[1]
gstenum_c = gst_enums[0]

gst_parse_sources = []
if gst_parse
  subdir('parse')
endif

subdir('printf')

libgst_c_args = gst_c_args + [
  '-D_GNU_SOURCE',
  '-DG_LOG_DOMAIN=g_log_domain_gstreamer',
  '-DGST_DISABLE_DEPRECATED',
]

tracer_hooks = get_option('tracer_hooks')
if not tracer_hooks
  libgst_c_args += ['-DGST_DISABLE_GST_TRACER_HOOKS']
endif

# Make sure that subproject building gir files work
gst_incdirs = [configinc]
gst_gen_sources = [gstenum_h]
libgst = library('gstreamer-1.0', gst_sources,
  gstenum_h, gstenum_c, gst_parse_sources, gst_registry_sources,
  objects : printf_lib.extract_all_objects(),
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  c_args : libgst_c_args + ['-DBUILDING_GST'],
  include_directories : [configinc,
    # HACK, change include paths in .y and .l in final version.
    include_directories('parse')],
  install : true,
  dependencies : [gobject_dep, gmodule_dep, glib_dep, mathlib, dl_dep] + backtrace_deps
                   + platform_deps,
)

if build_gir
  gst_gir_extra_args = gir_init_section + ['--c-include=gst/gst.h']
  if meson.is_subproject()
    # FIXME: There must be a better way to do this
    # Need to pass the include path to find gst/gst.h and gst/gstenumtypes.h (built)
    gst_gir_extra_args += ['--cflags-begin',
      '-I' + meson.current_source_dir() + '/..',
      '-I' + meson.current_build_dir() + '/..',
      '-DGST_DISABLE_MINIOBJECT_INLINE_FUNCTIONS',
      '--cflags-end']
  endif

  gst_incdirs += [configinc]
  gst_gir = gnome.generate_gir(libgst,
    sources : gst_sources + gst_headers + gst_enums + [gst_version_h],
    namespace : 'Gst',
    nsversion : apiversion,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-1.0',
    includes : ['GLib-2.0', 'GObject-2.0', 'GModule-2.0' ],
    install : true,
    extra_args : gst_gir_extra_args,
  )
  gst_gen_sources += gst_gir
endif

gst_compile_args = []
if get_option('default_library') == 'static'
  gst_compile_args += ['-DGST_STATIC_COMPILATION']
endif

gst_dep = declare_dependency(link_with : libgst,
  compile_args : gst_compile_args,
  include_directories : gst_incdirs,
  dependencies : [glib_dep, gobject_dep, gmodule_dep],
  # Everything that uses libgst needs this built to compile
  sources : gst_gen_sources,
)
