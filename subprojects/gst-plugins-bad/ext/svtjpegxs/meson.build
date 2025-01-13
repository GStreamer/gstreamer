svtjpegxs_sources = [
  'gstsvtjpegxs.c',
  'gstsvtjpegxsdec.c',
  'gstsvtjpegxsenc.c',
]

svtjpegxs_headers = [
  'gstsvtjpegxsdec.h',
  'gstsvtjpegxsenc.h',
]

doc_sources = []
foreach s: svtjpegxs_sources + svtjpegxs_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'svtjpegxs': pathsep.join(doc_sources)
}

svtjpegxs_dep = dependency('SvtJpegxs', version: '>= 0.9', required: get_option('svtjpegxs'))

if svtjpegxs_dep.found()
  gstsvtjpegxs = library('gstsvtjpegxs',
    svtjpegxs_sources,
    c_args: gst_plugins_bad_args, # FIXME: needed? + ['-DGST_USE_UNSTABLE_API'],
    include_directories: [configinc],
    dependencies: [gstbase_dep, gstvideo_dep, svtjpegxs_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  plugins += [gstsvtjpegxs]
endif
