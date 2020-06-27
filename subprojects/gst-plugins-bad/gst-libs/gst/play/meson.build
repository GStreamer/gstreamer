gstplay_sources = files(
  'gstplay.c',
  'gstplay-signal-adapter.c',
  'gstplay-video-renderer.c',
  'gstplay-media-info.c',
  'gstplay-video-overlay-video-renderer.c',
  'gstplay-visualization.c',
)

gstplay_headers = files(
  'play.h',
  'play-prelude.h',
  'gstplay.h',
  'gstplay-types.h',
  'gstplay-signal-adapter.h',
  'gstplay-video-renderer.h',
  'gstplay-media-info.h',
  'gstplay-video-overlay-video-renderer.h',
  'gstplay-visualization.h',
)

install_headers(gstplay_headers, subdir : 'gstreamer-' + api_version + '/gst/play/')

gstplay = library('gstplay-' + api_version,
  gstplay_sources,
  c_args : gst_plugins_bad_args + ['-DBUILDING_GST_PLAY'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : [gstbase_dep, gstvideo_dep, gstaudio_dep,
                  gsttag_dep, gstpbutils_dep],
)

pkgconfig.generate(gstplay,
  libraries : [gst_dep, gstvideo_dep],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : 'gstreamer-play-1.0',
  description : 'GStreamer Player convenience library',
)

gen_sources = []
if build_gir
  play_gir = gnome.generate_gir(gstplay,
    sources : gstplay_sources + gstplay_headers,
    namespace : 'GstPlay',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-play-1.0',
    includes : ['Gst-1.0', 'GstPbutils-1.0', 'GstBase-1.0', 'GstVideo-1.0',
      'GstAudio-1.0', 'GstTag-1.0'],
    install : true,
    extra_args : gir_init_section + ['-DGST_USE_UNSTABLE_API'] + ['--c-include=gst/play/play.h'],
    dependencies : [gstbase_dep, gstvideo_dep, gstaudio_dep,
                  gsttag_dep, gstpbutils_dep]
  )
  gen_sources += play_gir
endif

gstplay_dep = declare_dependency(link_with : gstplay,
  include_directories : [libsinc],
  sources: gen_sources,
  dependencies : [gstbase_dep, gstvideo_dep, gstaudio_dep,
                  gsttag_dep, gstpbutils_dep])

meson.override_dependency('gstreamer-play-1.0', gstplay_dep)
