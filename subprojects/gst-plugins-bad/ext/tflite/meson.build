tflite_sources = [
  'gsttflite.c',
  'gsttfliteinference.c',
  'modelinfo.c',
]

tflite_headers = [
  'gsttfliteinference.h',
  'gsttfliteedgetpuinference.h'
]

edgetpu_sources = [
  'gsttfliteedgetpuinference.c'
]

doc_sources = []
foreach s: tflite_sources + tflite_headers + edgetpu_sources
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'tflite': pathsep.join(doc_sources)
}

if get_option('tflite').disabled()
  subdir_done()
endif

tensorflow_lite_dep = cc.find_library('tensorflowlite_c',
				      required: get_option('tflite'))

tensorflow_lite_header_found = cc.has_header('tensorflow/lite/c/c_api.h',
					     dependencies: tensorflow_lite_dep,
					     required: get_option('tflite'))

if tensorflow_lite_dep.found() and tensorflow_lite_header_found
  tflite_extra_dep = []
  tflite_c_args = []

  if cc.has_header_symbol('tensorflow/lite/c/c_api.h', 'kTfLiteBFloat16',
			   dependencies: tensorflow_lite_dep)
    tflite_c_args += ['-DTFLITE_HAS_BFLOAT16']
  endif

  edgetpu_dep = cc.find_library('edgetpu',
				required : get_option('tflite-edgetpu'))

  if edgetpu_dep.found() and cc.has_header('libedgetpu/edgetpu_c.h',
                                           dependencies: edgetpu_dep,
                                           required: get_option('tflite-edgetpu'))
    tflite_c_args += ['-DEDGETPU','-DTFLITE_USE_OPAQUE_DELEGATE=0',
		     '-DTFLITE_WITH_STABLE_ABI=0']
    tflite_sources += edgetpu_sources
    tflite_extra_dep += [edgetpu_dep]
  endif


  gsttflite = library('gsttflite',
    tflite_sources,
    c_args : gst_plugins_bad_args + tflite_c_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gstvideo_dep, gstanalytics_dep,
                    tensorflow_lite_dep,libm, gio_dep, tflite_extra_dep],
    install : true,
    install_dir : plugins_install_dir,
  )

  plugins += [gsttflite]
endif
