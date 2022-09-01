rpicamsrc_sources = [
  'gstrpicamsrc.c',
  'gstrpicamsrcdeviceprovider.c',
  'RaspiCapture.c',
  'RaspiCamControl.c',
  'RaspiPreview.c',
  'RaspiCLI.c',
]

if host_system != 'linux' or (host_cpu != 'arm' and host_cpu != 'aarch64')
  assert(not get_option('rpicamsrc').enabled(), 'rpicamsrc was enabled by options but will not be built')
  subdir_done()
endif

if get_option('rpicamsrc').disabled()
  subdir_done()
endif

rpi_inc_path = get_option('rpi-header-dir')
rpi_lib_path = get_option('rpi-lib-dir')

rpi_inc_args = [
  '-I' + rpi_inc_path,
  '-I' + join_paths(rpi_inc_path, 'interface', 'vcos', 'pthreads'),
  '-I' + join_paths(rpi_inc_path, 'interface', 'vmcs_host', 'linux'),
]

if not cc.has_header('bcm_host.h', args: rpi_inc_args)
  if get_option('rpicamsrc').enabled()
    error('Could not find bcm_host.h. Please pass the location of this header via -Drpi-header-dir=/path')
  else
    subdir_done()
  endif
endif

thread_dep = dependency('threads')
rt_dep = cxx.find_library('rt', required : false)

mmal_deps = [thread_dep, rt_dep]
foreach rpi_lib : ['mmal_core', 'mmal_util', 'mmal_vc_client', 'vcos', 'vchostif', 'vchiq_arm', 'bcm_host']
  l = cc.find_library(rpi_lib, dirs: rpi_lib_path, required: false)
  if not l.found()
    if get_option('rpicamsrc').enabled()
      error('''
            Could not find lib@0@ in standard library paths and @1@.
	    Please pass the location of these libs via -Dwith-rpi-lib-dir=/path.
	    '''.format(rpi_lib, rpi_lib_path))
    else
      subdir_done()
    endif
  endif
  mmal_deps += [l]
endforeach

rpi_warn_flags = cc.get_supported_arguments([
  # vcos/mmal headers
  '-Wno-redundant-decls',
  # RaspiCapture.c + RaspiCamControl.c
  '-Wno-discarded-qualifiers',
  '-Wno-declaration-after-statement',
  '-Wno-missing-prototypes',
  '-Wno-missing-declarations',
  '-Wno-stringop-truncation',
])

rpi_link_flags = cc.get_supported_link_arguments(['-Wl,--no-as-needed'])

gnome = import('gnome')

enums = gnome.mkenums_simple('gstrpicam-enum-types',
  sources: 'gstrpicam_types.h',
  identifier_prefix: 'GstRpiCamSrc',
  symbol_prefix: 'gst_rpi_cam_src')

gstrpicamsrc = library('gstrpicamsrc',
  rpicamsrc_sources, enums,
  c_args: [gst_plugins_good_args, rpi_inc_args, rpi_warn_flags],
  link_args: rpi_link_flags,
  include_directories: [configinc, libsinc],
  dependencies: [gst_dep, gstbase_dep, gstvideo_dep] + mmal_deps,
  override_options: ['b_asneeded=false'],
  install_dir: plugins_install_dir,
  install: true)

plugins += [gstrpicamsrc]
