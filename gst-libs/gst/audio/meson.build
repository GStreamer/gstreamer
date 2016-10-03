audio_src= [
  'audio.c',
  'audio-channel-mixer.c',
  'audio-channels.c',
  'audio-converter.c',
  'audio-format.c',
  'audio-info.c',
  'audio-quantize.c',
  'audio-resampler.c',
  'gstaudiobasesink.c',
  'gstaudiobasesrc.c',
  'gstaudiocdsrc.c',
  'gstaudioclock.c',
  'gstaudiodecoder.c',
  'gstaudioencoder.c',
  'gstaudiofilter.c',
  'gstaudioiec61937.c',
  'gstaudiometa.c',
  'gstaudioringbuffer.c',
  'gstaudiosink.c',
  'gstaudiosrc.c',
  'gstaudioutilsprivate.c',
  'streamvolume.c',
]

audio_mkenum_headers = [
  'audio.h',
  'audio-format.h',
  'audio-channels.h',
  'audio-channel-mixer.h',
  'audio-converter.h',
  'audio-info.h',
  'audio-quantize.h',
  'audio-resampler.h',
  'gstaudioringbuffer.h',
]

# FIXME: check headers
audio_headers = audio_mkenum_headers + [
  'gstaudiobasesink.h',
  'gstaudiobasesrc.h',
  'gstaudiocdsrc.h',
  'gstaudioclock.h',
  'gstaudiodecoder.h',
  'gstaudioencoder.h',
  'gstaudiofilter.h',
  'gstaudioiec61937.h',
  'gstaudiometa.h',
  'gstaudiosink.h',
  'gstaudiosrc.h',
  'streamvolume.h',
]
install_headers(audio_headers, subdir : 'gstreamer-1.0/gst/audio/')

mkenums = find_program('audio_mkenum.py')
gstaudio_h = custom_target('gstaudioenum_h',
  output : 'audio-enumtypes.h',
  input : audio_mkenum_headers,
  install : true,
  install_dir : 'include/gstreamer-1.0/gst/audio/',
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

gstaudio_c = custom_target('gstaudioenum_c',
  output : 'audio-enumtypes.c',
  input : audio_mkenum_headers,
  depends : [gstaudio_h],
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

orcsrc = 'gstaudiopack'
if have_orcc
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

simd_cargs = []
simd_dependencies = []

if have_sse
  audio_resampler_sse = static_library('audio_resampler_sse',
    [ 'audio-resampler-x86-sse.c' ],
    c_args : gst_plugins_base_args + [sse_args] + [pic_args],
    include_directories : [configinc, libsinc],
    dependencies : [gst_base_dep],
    install : false
  )

  simd_cargs += ['-DHAVE_SSE']
  simd_dependencies += audio_resampler_sse
endif

if have_sse2
  audio_resampler_sse2 = static_library('audio_resampler_sse2',
    [ 'audio-resampler-x86-sse2.c' ],
    c_args : gst_plugins_base_args + [sse2_args] + [pic_args],
    include_directories : [configinc, libsinc],
    dependencies : [gst_base_dep],
    install : false
  )

  simd_cargs += ['-DHAVE_SSE2']
  simd_dependencies += audio_resampler_sse2
endif

if have_sse41
  audio_resampler_sse41 = static_library('audio_resampler_sse41',
    [ 'audio-resampler-x86-sse41.c' ],
    c_args : gst_plugins_base_args + [sse41_args] + [pic_args],
    include_directories : [configinc, libsinc],
    dependencies : [gst_base_dep],
    install : false
  )

  simd_cargs += ['-DHAVE_SSE41']
  simd_dependencies += audio_resampler_sse41
endif

gstaudio = library('gstaudio-@0@'.format(api_version),
  audio_src, gstaudio_h, gstaudio_c, orc_c, orc_h,
  c_args : gst_plugins_base_args + simd_cargs,
  include_directories: [configinc, libsinc],
  link_with : simd_dependencies,
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [tag_dep, gst_base_dep, orc_dep, libm],
  vs_module_defs: vs_module_defs_dir + 'libgstaudio.def',
)
# TODO: generate gir

audio_dep = declare_dependency(link_with : gstaudio,
  include_directories : [libsinc],
  dependencies : [gst_base_dep, tag_dep],
  sources : [gstaudio_h])
