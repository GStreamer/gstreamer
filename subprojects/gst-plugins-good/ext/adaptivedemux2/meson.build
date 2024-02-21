dash_sources = [
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
  'dash/gstmpdclient.c'
]

smoothstreaming_sources = [
  'mss/gstmssdemux.c',
  'mss/gstmssmanifest.c',
  'mss/gstmssfragmentparser.c',
]

plugin_sources = [
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

# Used for unit tests, so need to be defined even if we skip the subdir
hls_dep = dependency('', required : false)
adaptivedemux2_dep = dependency('', required : false)

adaptivedemux2_opt = get_option('adaptivedemux2')
soup_opt = get_option('soup')
soup_ver_opt = get_option('soup-version')
if adaptivedemux2_opt.disabled()
  message('Not building adaptivedemux2 plugin because it was disabled')
  subdir_done()
endif

adaptive_xml2_dep = dependency('libxml-2.0', version : '>= 2.8', allow_fallback: true, required: adaptivedemux2_opt)

if not adaptive_xml2_dep.found()
  message(f'Not building adaptivedemux2 plugin: libxml2 is needed')
  subdir_done()
endif

subdir('hls')

plugin_sources += dash_sources
plugin_sources += smoothstreaming_sources
plugin_sources += hls_sources

libdl = cc.find_library('dl', required: false)
soup_loader_args = ['-DBUILDING_ADAPTIVEDEMUX2']
soup_link_args = []
soup_link_deps = []

default_library = get_option('default_library')
if host_system != 'linux' or default_library in ['static', 'both']
  if soup_ver_opt in ['auto', '3']
    libsoup3_dep = dependency('libsoup-3.0', allow_fallback: true,
                              required: soup_ver_opt == '3' and soup_opt.enabled())
  endif
  if soup_ver_opt in ['auto', '2']
    libsoup2_dep = dependency('libsoup-2.4', version : '>=2.48', allow_fallback: true,
                              default_options: ['sysprof=disabled'],
                              required: soup_ver_opt == '2' and soup_opt.enabled())
  endif

  if libsoup3_dep.found()
    soup_link_deps += [libsoup3_dep]
    soup_link_args = ['-DLINK_SOUP=3']
  elif libsoup2_dep.found()
    soup_link_deps += [libsoup2_dep]
    soup_link_args = ['-DLINK_SOUP=2']
  else
    if adaptivedemux2_opt.enabled()
      error(f'adaptivedemux2: Either libsoup2 or libsoup3 is needed')
    endif
    message(f'Not building adaptivedemux2 plugin: either libsoup2 or libsoup3 is needed')
    subdir_done()
  endif
endif

# Shared plugin doesn't link to libsoup but dlopen()s it at runtime
adaptive_kwargs = {
  'sources': plugin_sources,
  'include_directories': [configinc, libsinc],
  'install': true,
  'install_dir': plugins_install_dir,
}
adaptive_deps = [gmodule_dep, gst_dep, gsttag_dep, gstnet_dep, gstbase_dep, gstpbutils_dep,
    gstapp_dep, gio_dep, adaptive_xml2_dep, hls_crypto_dep, libdl, libm]
adaptive_args = [gst_plugins_good_args, soup_loader_args, hls_cargs,
    '-DGST_ISOFF_API=G_GNUC_INTERNAL']

if host_system != 'linux'
    adaptivedemux2 = library('gstadaptivedemux2',
      c_args: [adaptive_args, soup_link_args],
      dependencies: [adaptive_deps, soup_link_deps],
      kwargs: adaptive_kwargs)
    adaptivedemux2_static = adaptivedemux2
    adaptivedemux2_shared = adaptivedemux2
else
  if default_library in ['static', 'both']
    # Static plugin links to libsoup directly at build time
    adaptivedemux2_static = static_library('gstadaptivedemux2',
      c_args: [adaptive_args, soup_link_args],
      dependencies: [adaptive_deps, soup_link_deps],
      kwargs: adaptive_kwargs)
  endif
  if default_library in ['shared', 'both']
    adaptivedemux2_shared = shared_library('gstadaptivedemux2',
      c_args: adaptive_args,
      dependencies: adaptive_deps,
      kwargs: adaptive_kwargs)
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
