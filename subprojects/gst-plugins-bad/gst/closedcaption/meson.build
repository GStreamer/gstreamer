closedcaption_sources = [
    'gstcccombiner.c',
    'gstccextractor.c',
    'gstccconverter.c',
    'gstcea608mux.c',
    'gstclosedcaption.c',
    'gstline21dec.c',
    'gstline21enc.c',
    'ccutils.c',
    'gsth264ccextractor.c',
    'gsth265ccextractor.c',
    'gsth264reorder.c',
    'gsth265reorder.c',
    'gstcodecseiinserter.c',
    'gsth264seiinserter.c',
    'gsth265seiinserter.c',
]

closedcaption_headers = [
  'gstline21dec.h',
  'gstcccombiner.h',
  'gstcea608mux.h',
  'gstccconverter.h',
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

if get_option('closedcaption').disabled()
  subdir_done()
endif

gstclosedcaption = library('gstclosedcaption',
  closedcaption_sources,
  zvbi_sources,
  c_args : gst_plugins_bad_args + extra_args,
  link_args : noseh_link_args,
  include_directories : [configinc],
  dependencies : [gstvideo_dep, gstbase_dep, gst_dep, libm,
                  gstcodecs_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstclosedcaption]
