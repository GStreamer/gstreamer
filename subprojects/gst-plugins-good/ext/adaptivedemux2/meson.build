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

soup_loader_args = ['-DBUILDING_ADAPTIVEDEMUX2']

default_library = get_option('default_library')
if default_library in ['static', 'both']
  libsoup2_dep = dependency('libsoup-2.4', version : '>=2.48',
                            required : false, fallback : ['libsoup', 'libsoup_dep'],
                            default_options: ['sysprof=disabled'])
  libsoup3_dep = dependency('libsoup-3.0', required : false,
                            fallback : ['libsoup3', 'libsoup_dep'])

  if libsoup3_dep.found()
    soup_dep = libsoup3_dep
    static_soup_loader_args = ['-DSTATIC_SOUP=3']
  elif libsoup2_dep.found()
    soup_dep = libsoup2_dep
    static_soup_loader_args = ['-DSTATIC_SOUP=2']
  else
    if adaptivedemux2_opt.enabled()
      error(f'adaptivedemux2: Either libsoup2 or libsoup3 is needed for build with default_library=@default_library@')
    endif

    message(f'Not building adaptivedemux2 plugin: either libsoup2 or libsoup3 is needed for build with default_library=@default_library@')
    subdir_done()
  endif

  # Static plugin links to libsoup directly at build time
  adaptivedemux2_static = static_library('gstadaptivedemux2',
    plugin_sources,
    include_directories: [configinc, libsinc],
    c_args: [gst_plugins_good_args, soup_loader_args, soup_loader_args, hls_cargs,
             '-DGST_ISOFF_API=G_GNUC_INTERNAL'],
    dependencies: [gst_dep, gsttag_dep, gstnet_dep, gstbase_dep,
                   gstpbutils_dep, gstapp_dep, soup_dep,
                   gio_dep, adaptive_xml2_dep,
                   hls_crypto_dep, libm],
    install: true,
    install_dir: plugins_install_dir)
endif

if default_library in ['shared', 'both']
  # Shared plugin doesn't link to libsoup but dlopen()s it at runtime
  libdl = cc.find_library('dl', required: false)

  adaptivedemux2_shared = shared_library('gstadaptivedemux2',
    plugin_sources,
    include_directories: [configinc, libsinc],
    c_args: [gst_plugins_good_args, soup_loader_args, hls_cargs,
             '-DGST_ISOFF_API=G_GNUC_INTERNAL'],
    dependencies: [gsttag_dep, gstnet_dep, gstbase_dep,
                   gstpbutils_dep, gstapp_dep, gio_dep,
                   gmodule_dep, adaptive_xml2_dep,
                   hls_crypto_dep, libm, libdl],
    install: true,
    install_dir: plugins_install_dir)
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
