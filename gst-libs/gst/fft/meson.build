fft_sources = [
  'gstfft.c',
  'gstffts16.c',
  'gstffts32.c',
  'gstfftf32.c',
  'gstfftf64.c',
  'kiss_fft_s16.c',
  'kiss_fft_s32.c',
  'kiss_fft_f32.c',
  'kiss_fft_f64.c',
  'kiss_fftr_s16.c',
  'kiss_fftr_s32.c',
  'kiss_fftr_f32.c',
  'kiss_fftr_f64.c'
]

fft_headers = [
  'fft.h',
  'gstfftf32.h',
  'gstfftf64.h',
  'gstfft.h',
  'gstffts16.h',
  'gstffts32.h',
]
install_headers(fft_headers, subdir : 'gstreamer-1.0/gst/fft/')

gstfft = library('gstfft-@0@'.format(api_version),
  fft_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, libm],
  vs_module_defs: vs_module_defs_dir + 'libgstfft.def',
)

fft_gen_sources  = []
if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/fft/fft.h' ]
  fft_gen_sources += [gnome.generate_gir(gstfft,
    sources : fft_sources + fft_headers,
    namespace : 'GstFft',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-fft-1.0',
    includes : ['Gst-1.0'],
    install : true,
    extra_args : gst_gir_extra_args,
    dependencies : [gst_dep, libm]
  )]
endif

fft_dep = declare_dependency(link_with: gstfft,
  include_directories : [libsinc],
  dependencies : [gst_dep],
  sources: fft_gen_sources)
