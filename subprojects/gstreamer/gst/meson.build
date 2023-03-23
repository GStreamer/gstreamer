gst_sources = files(
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
)

gst_headers = files(
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
)
if host_system == 'darwin'
  gst_headers += 'gstmacos.h'
  gst_sources += 'gstmacos.m'
endif

install_headers(gst_headers, subdir : 'gstreamer-1.0/gst')

if host_system == 'windows'
  gst_sources += files('gstpluginloader-win32.c')
else
  gst_sources += files('gstpluginloader.c')
endif

extra_deps = []
if host_system == 'android'
  gst_sources += 'gstandroid.c'
  extra_deps += cc.find_library('log')
endif

if host_system == 'darwin'
  extra_deps += dependency('appleframeworks', modules : ['Cocoa'])
endif

gst_registry = get_option('registry')
if gst_registry
  gst_registry_sources = files('gstregistrybinary.c')
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
  '-DG_LOG_DOMAIN="GStreamer"',
  '-DGST_DISABLE_DEPRECATED',
]

tracer_hooks = get_option('tracer_hooks')
if not tracer_hooks
  libgst_c_args += ['-DGST_DISABLE_GST_TRACER_HOOKS']
endif

if get_option('gstreamer-full').enabled()
  libgst_c_args += ['-DGST_FULL_COMPILATION']
endif

# Make sure that subproject building gir files work
gst_incdirs = [configinc]
gst_gen_sources = [gstenum_h]
libgst = library('gstreamer-1.0', gst_sources,
  gstenum_h, gstenum_c, gst_parse_sources, gst_registry_sources,
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  c_args : libgst_c_args + ['-DBUILDING_GST'],
  include_directories : [configinc,
    # HACK, change include paths in .y and .l in final version.
    include_directories('parse')],
  link_with : printf_lib,
  install : true,
  dependencies : [gobject_dep, gmodule_dep, glib_dep, mathlib, dl_dep,
                  backtrace_deps, platform_deps, extra_deps],
)

gst_compile_args = []
if static_build
  gst_compile_args += ['-DGST_STATIC_COMPILATION']
endif

pkg_name = 'gstreamer-1.0'
library_def = {'lib': libgst}
pkgconfig.generate(libgst,
  libraries : [glib_dep, gobject_dep] + pkgconfig_libs,
  variables : pkgconfig_variables,
  uninstalled_variables: pkgconfig_uninstalled_variables,
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'Streaming media framework',
  extra_cflags: gst_compile_args,
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
  gir = {
    'sources' : gst_sources + gst_headers + gst_enums + [gst_version_h],
    'namespace' : 'Gst',
    'nsversion' : apiversion,
    'identifier_prefix' : 'Gst',
    'symbol_prefix' : 'gst',
    'export_packages' : pkg_name,
    'includes' : ['GLib-2.0', 'GObject-2.0', 'GModule-2.0' ],
    'install' : true,
    'extra_args' : gst_gir_extra_args,
  }
  library_def += {'gir': [gir]}
  # When building statically the information about the gir is going to be
  # consumed by `gst-build` to generate gir files linked against
  # `libgstreamer-full-1.0.so`. We do not build it here as it is currently
  # broken. Once that is fixed we could build it here, even though we would keep
  # creating another gir which links against gst-full.
  if not static_build
    gst_gir = gnome.generate_gir(libgst, kwargs: gir)
    library_def += {'gir_targets':  library_def.get('gir_targets', []) + [gst_gir]}
    gst_gen_sources += gst_gir
  endif
endif
gst_libraries += [[pkg_name, library_def]]



gst_dep = declare_dependency(link_with : libgst,
  compile_args : gst_compile_args,
  include_directories : gst_incdirs,
  dependencies : [glib_dep, gobject_dep, gmodule_dep],
  # Everything that uses libgst needs this built to compile
  sources : gst_gen_sources,
)

meson.override_dependency(pkg_name, gst_dep)
