adaptive_sources = [
  'dash/gstdashdemux.c',
  'dash/gstmpdnode.c',
  'dash/gstmpdrootnode.c',
  'dash/gstmpdbaseurlnode.c',
  'dash/gstmpdutctimingnode.c',
  'dash/gstmpdmetricsnode.c',
  'dash/gstmpdmetricsrangenode.c',
  'dash/gstmpdsnode.c',
  'dash/gstmpdsegmenttimelinenode.c',
  'dash/gstmpdsegmenttemplatenode.c',
  'dash/gstmpdsegmenturlnode.c',
  'dash/gstmpdsegmentlistnode.c',
  'dash/gstmpdsegmentbasenode.c',
  'dash/gstmpdperiodnode.c',
  'dash/gstmpdrepresentationbasenode.c',
  'dash/gstmpdmultsegmentbasenode.c',
  'dash/gstmpdrepresentationnode.c',
  'dash/gstmpdsubrepresentationnode.c',
  'dash/gstmpdcontentcomponentnode.c',
  'dash/gstmpdadaptationsetnode.c',
  'dash/gstmpdsubsetnode.c',
  'dash/gstmpdprograminformationnode.c',
  'dash/gstmpdlocationnode.c',
  'dash/gstmpdreportingnode.c',
  'dash/gstmpdurltypenode.c',
  'dash/gstmpddescriptortypenode.c',
  'dash/gstxmlhelper.c',
  'dash/gstmpdhelper.c',
  'dash/gstmpdparser.c',
  'dash/gstmpdclient.c',
  'mss/gstmssdemux.c',
  'mss/gstmssmanifest.c',
  'mss/gstmssfragmentparser.c',
  'plugin.c',
  'gstisoff.c',
  'gstadaptivedemux.c',
  'gstadaptivedemuxelement.c',
  'gstadaptivedemuxutils.c',
  'gstadaptivedemux-period.c',
  'gstadaptivedemux-stream.c',
  'gstadaptivedemux-track.c',
  'downloadhelper.c',
  'downloadrequest.c',
  '../soup/gstsouploader.c'
]

adaptive_headers = [
  'downloadhelper.h',
  'downloadrequest.h',
  'gstadaptivedemuxelements.h',
  'gstadaptivedemux.h',
  'gstadaptivedemux-private.h',
  'gstadaptivedemux-stream.h',
  'gstadaptivedemux-types.h',
  'gstadaptivedemuxutils.h',
  'gstisoff.h',
  'dash/gstdash_debug.h',
  'dash/gstdashdemux.h',
  'dash/gstmpdadaptationsetnode.h',
  'dash/gstmpdbaseurlnode.h',
  'dash/gstmpdclient.h',
  'dash/gstmpdcontentcomponentnode.h',
  'dash/gstmpddescriptortypenode.h',
  'dash/gstmpdhelper.h',
  'dash/gstmpdlocationnode.h',
  'dash/gstmpdmetricsnode.h',
  'dash/gstmpdmetricsrangenode.h',
  'dash/gstmpdmultsegmentbasenode.h',
  'dash/gstmpdnode.h',
  'dash/gstmpdparser.h',
  'dash/gstmpdperiodnode.h',
  'dash/gstmpd-prelude.h',
  'dash/gstmpdprograminformationnode.h',
  'dash/gstmpdreportingnode.h',
  'dash/gstmpdrepresentationbasenode.h',
  'dash/gstmpdrepresentationnode.h',
  'dash/gstmpdrootnode.h',
  'dash/gstmpdsegmentbasenode.h',
  'dash/gstmpdsegmentlistnode.h',
  'dash/gstmpdsegmenttemplatenode.h',
  'dash/gstmpdsegmenttimelinenode.h',
  'dash/gstmpdsegmenturlnode.h',
  'dash/gstmpdsnode.h',
  'dash/gstmpdsubrepresentationnode.h',
  'dash/gstmpdsubsetnode.h',
  'dash/gstmpdurltypenode.h',
  'dash/gstmpdutctimingnode.h',
  'dash/gstxmlhelper.h',
  'mss/gstmssdemux.h',
  'mss/gstmssfragmentparser.h',
  'mss/gstmssmanifest.h',
  'mss/gstmss-prelude.h',
]

subdir('hls')

adaptive_sources += hls_sources
adaptive_headers += hls_headers
doc_sources = []
foreach s: adaptive_sources + adaptive_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'adaptivedemux2': pathsep.join(doc_sources)
}

# Used for unit tests, so need to be defined even if we skip the subdir
hls_dep = dependency('', required : false)
adaptivedemux2_dep = dependency('', required : false)

adaptivedemux2_opt = get_option('adaptivedemux2')
if adaptivedemux2_opt.disabled()
  message('Not building adaptivedemux2 plugin because it was disabled')
  subdir_done()
endif

if soup_lookup_dep and not libsoup3_dep.found() and not libsoup2_dep.found()
  if adaptivedemux2_opt.enabled()
    error(f'adaptivedemux2: Either libsoup2 or libsoup3 is needed')
  endif
  message(f'Not building adaptivedemux2: must link to either libsoup2 or libsoup3')
  subdir_done()
endif

adaptive_xml2_dep = dependency('libxml-2.0', version : '>= 2.8', allow_fallback: true, required: adaptivedemux2_opt)

if not adaptive_xml2_dep.found()
  message(f'Not building adaptivedemux2 plugin: libxml2 is needed')
  subdir_done()
endif

libdl = cc.find_library('dl', required: false)
soup_loader_args = ['-DBUILDING_ADAPTIVEDEMUX2']

# Shared plugin doesn't link to libsoup but dlopen()s it at runtime
adaptive_kwargs = {
  'sources': adaptive_sources,
  'include_directories': [configinc, libsinc],
  'install': true,
  'install_dir': plugins_install_dir,
}
adaptive_deps = [gmodule_dep, gst_dep, gsttag_dep, gstnet_dep, gstbase_dep, gstpbutils_dep,
    gstapp_dep, gio_dep, adaptive_xml2_dep, hls_crypto_dep, libdl, libm]
adaptive_args = [gst_plugins_good_args, soup_loader_args, hls_cargs,
    '-DGST_ISOFF_API=G_GNUC_INTERNAL']

if host_system == 'windows'
  assert(soup_linked_target)
  adaptivedemux2 = library('gstadaptivedemux2',
    c_args: [adaptive_args, soup_linked_target_args],
    dependencies: [adaptive_deps, soup_linked_target_deps],
    kwargs: adaptive_kwargs)
  adaptivedemux2_static = adaptivedemux2
  adaptivedemux2_shared = adaptivedemux2
else
  if default_library in ['static', 'both']
    # Static plugin links to libsoup directly at build time
    assert(soup_linked_target)
    adaptivedemux2_static = static_library('gstadaptivedemux2',
      c_args: [adaptive_args, soup_linked_target_args],
      dependencies: [adaptive_deps, soup_linked_target_deps],
      kwargs: adaptive_kwargs)
  endif
  if default_library in ['shared', 'both']
    adaptivedemux2_shared = shared_library('gstadaptivedemux2',
      c_args: adaptive_args,
      dependencies: [adaptive_deps, soup_dlopen_target_deps],
      kwargs: adaptive_kwargs + soup_dlopen_target_kwargs)
  endif
endif

# Use the static library to generate the .pc file if it's available. The shared
# library .pc file does not have a Requires: on libsoup, and we use plugin
# .pc files to generate dependencies for linking plugins statically.
if default_library == 'shared'
  pkgconfig.generate(adaptivedemux2_shared, install_dir: plugins_pkgconfig_install_dir)
else
  pkgconfig.generate(adaptivedemux2_static, install_dir: plugins_pkgconfig_install_dir)
endif

# Add the shared library to the plugins list if available. We pass this list of
# plugins to hotdoc to generate the plugins cache, which introspects the plugin
# by loading it. We need the shared plugin for that.
if default_library == 'static'
  plugins += [adaptivedemux2_static]
else
  plugins += [adaptivedemux2_shared]
endif

# For unit tests
adaptivedemux2_dep = declare_dependency(
  include_directories : include_directories('.'),
  dependencies: adaptive_xml2_dep)
