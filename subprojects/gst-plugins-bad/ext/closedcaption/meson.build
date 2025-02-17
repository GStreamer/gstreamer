closedcaption_dep = dependency('pangocairo', version : '>= 1.32.6',
                            required : get_option('closedcaption'))

closedcaption_sources = [
    'gstcccombiner.c',
    'gstccextractor.c',
    'gstccconverter.c',
    'gstcea608mux.c',
    'gstclosedcaption.c',
    'gstline21dec.c',
    'gstcea708decoder.c',
    'gstceaccoverlay.c',
    'gstline21enc.c',
    'ccutils.c',
    'gsth264ccextractor.c',
    'gsth265ccextractor.c',
]

closedcaption_headers = [
  'gstline21dec.h',
  'gstcea708decoder.h',
  'gstcccombiner.h',
  'gstcea608mux.h',
  'gstccconverter.h',
  'gstceaccoverlay.h',
  'gstccextractor.h',
  'ccutils.h',
  'gstline21enc.h',
]

zvbi_sources = [
  'bit_slicer.c',
  'decoder.c',
  'raw_decoder.c',
  'sampling_par.c',
  'io-sim.c',
]

extra_args = ['-DGST_USE_UNSTABLE_API']

doc_sources = []
foreach s: closedcaption_sources + closedcaption_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'closedcaption': pathsep.join(doc_sources)
}

if closedcaption_dep.found()
  gstclosedcaption = library('gstclosedcaption',
    closedcaption_sources,
    zvbi_sources,
    c_args : gst_plugins_bad_args + extra_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, gstbase_dep, gst_dep, closedcaption_dep, libm,
                    gstcodecs_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstclosedcaption]
endif
