omx_sources = [
  'gstomx.c',
  'gstomxallocator.c',
  'gstomxbufferpool.c',
  'gstomxvideo.c',
  'gstomxvideodec.c',
  'gstomxvideoenc.c',
  'gstomxaudiodec.c',
  'gstomxaudioenc.c',
  'gstomxmjpegdec.c',
  'gstomxmpeg4videodec.c',
  'gstomxmpeg2videodec.c',
  'gstomxh264dec.c',
  'gstomxh264utils.c',
  'gstomxh263dec.c',
  'gstomxwmvdec.c',
  'gstomxmpeg4videoenc.c',
  'gstomxh264enc.c',
  'gstomxh263enc.c',
  'gstomxaacdec.c',
  'gstomxmp3dec.c',
  'gstomxaacenc.c',
  'gstomxamrdec.c',
  'gstomxaudiosink.c',
  'gstomxanalogaudiosink.c',
  'gstomxhdmiaudiosink.c',
  'gstomxmp3enc.c',
]

extra_c_args = []

if have_omx_vp8
  omx_sources += 'gstomxvp8dec.c'
endif

if have_omx_theora
  omx_sources += 'gstomxtheoradec.c'
endif

if have_omx_hevc
  omx_sources += 'gstomxh265utils.c'
  omx_sources += 'gstomxh265enc.c'
  omx_sources += 'gstomxh265dec.c'
endif

optional_deps = []
if gstgl_dep.found()
  optional_deps += gstgl_dep
  extra_c_args += ['-DGST_USE_UNSTABLE_API']
endif

gstomx = library('gstomx',
  omx_sources,
  c_args : gst_omx_args + extra_c_args,
#  link_args : noseh_link_args,
  include_directories : [configinc, omx_inc],
  dependencies : [gstvideo_dep, gstaudio_dep, gstbase_dep, gstcontroller_dep,
                  libm, gmodule_dep, gstallocators_dep] + optional_deps,
  install : true,
  install_dir : plugins_install_dir,
)

plugins = [gstomx]
