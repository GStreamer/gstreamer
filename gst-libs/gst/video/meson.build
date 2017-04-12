video_sources = [
  'colorbalance.c',
  'colorbalancechannel.c',
  'convertframe.c',
  'gstvideoaffinetransformationmeta.c',
  'gstvideodecoder.c',
  'gstvideoencoder.c',
  'gstvideofilter.c',
  'gstvideometa.c',
  'gstvideopool.c',
  'gstvideosink.c',
  'gstvideotimecode.c',
  'gstvideoutils.c',
  'gstvideoutilsprivate.c',
  'navigation.c',
  'video.c',
  'video-blend.c',
  'video-chroma.c',
  'video-color.c',
  'video-converter.c',
  'video-dither.c',
  'video-event.c',
  'video-format.c',
  'video-frame.c',
  'video-info.c',
  'video-multiview.c',
  'video-resampler.c',
  'video-scaler.c',
  'video-tile.c',
  'video-overlay-composition.c',
  'videodirection.c',
  'videoorientation.c',
  'videooverlay.c',
]

video_headers = [
  'colorbalance.h',
  'colorbalancechannel.h',
  'gstvideoaffinetransformationmeta.h',
  'gstvideodecoder.h',
  'gstvideoencoder.h',
  'gstvideofilter.h',
  'gstvideometa.h',
  'gstvideopool.h',
  'gstvideosink.h',
  'gstvideotimecode.h',
  'gstvideoutils.h',
  'navigation.h',
  'video.h',
  'video-event.h',
  'video-format.h',
  'video-chroma.h',
  'video-color.h',
  'video-converter.h',
  'video-dither.h',
  'video-info.h',
  'video-frame.h',
  'video-scaler.h',
  'video-tile.h',
  'videodirection.h',
  'videoorientation.h',
  'videooverlay.h',
  'video-resampler.h',
  'video-blend.h',
  'video-overlay-composition.h',
  'video-multiview.h',
]
install_headers(video_headers, subdir : 'gstreamer-1.0/gst/video/')

video_mkenum_headers = [
  'video.h',
  'video-format.h',
  'video-frame.h',
  'video-chroma.h',
  'video-color.h',
  'video-converter.h',
  'video-dither.h',
  'video-info.h',
  'video-resampler.h',
  'video-scaler.h',
  'video-tile.h',
  'colorbalance.h',
  'navigation.h',
]

mkenums = find_program('video_mkenum.py')
gstvideo_h = custom_target('gstvideoenum_h',
  output : 'video-enumtypes.h',
  input : video_mkenum_headers,
  install : true,
  install_dir : 'include/gstreamer-1.0/gst/video/',
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

gstvideo_c = custom_target('gstvideoenum_c',
  output : 'video-enumtypes.c',
  input : video_mkenum_headers,
  depends : [gstvideo_h],
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])
video_gen_sources = [gstvideo_h]

orcsrc = 'video-orc'
gstvideo_deps = [gst_base_dep, libm]
if have_orcc
  gstvideo_deps += [orc_dep]
  orc_h = custom_target(orcsrc + '.h',
    input : orcsrc + '.orc',
    output : orcsrc + '.h',
    command : orcc_args + ['--header', '-o', '@OUTPUT@', '@INPUT@'])
  orc_c = custom_target(orcsrc + '.c',
    input : orcsrc + '.orc',
    output : orcsrc + '.c',
    command : orcc_args + ['--implementation', '-o', '@OUTPUT@', '@INPUT@'])
else
  orc_h = configure_file(input : orcsrc + '-dist.h',
    output : orcsrc + '.h',
    configuration : configuration_data())
  orc_c = configure_file(input : orcsrc + '-dist.c',
    output : orcsrc + '.c',
    configuration : configuration_data())
endif

gstvideo = library('gstvideo-@0@'.format(api_version),
  video_sources, gstvideo_h, gstvideo_c, orc_c, orc_h,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : gstvideo_deps,
  vs_module_defs: vs_module_defs_dir + 'libgstvideo.def',
)

if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/video/video.h' ]
  video_gen_sources += [gnome.generate_gir(gstvideo,
    sources : video_sources + video_headers + [gstvideo_c] + [gstvideo_h],
    namespace : 'GstVideo',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-video-1.0',
    includes : ['Gst-1.0', 'GLib-2.0', 'GObject-2.0', 'GModule-2.0' ],
    install : true,
    extra_args : gst_gir_extra_args,
    dependencies : gstvideo_deps
  )]
endif

video_dep = declare_dependency(link_with : gstvideo,
  include_directories : [libsinc],
  dependencies : gstvideo_deps,
  sources : video_gen_sources)
