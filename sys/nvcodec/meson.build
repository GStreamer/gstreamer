nvcodec_sources = [
  'plugin.c',
  'gstnvenc.c',
  'gstnvbaseenc.c',
  'gstnvh264enc.c',
  'gstnvh265enc.c',
  'gstcudaloader.c',
]

nvdec_sources = [
  'gstnvdec.c',
  'gstcuvidloader.c',
]

use_nvcodec_gl = false
extra_c_args = []

cuda_dep = dependency('', required : false)
cuda_header_dep = dependency('', required : false)
cuda_incdir = ''

nvcodec_option = get_option('nvcodec')
if nvcodec_option.disabled()
  subdir_done()
endif

cuda_versions = [
    '10.1',
    '10.0',
    '9.2',
    '9.1',
    '9.0',
    '8.0',
    '7.5',
    '7.0',
    '6.5',
  ]
cuda_ver = ''

cuda_dep = dependency('cuda', required:false)
if cuda_dep.found()
  cuda_ver = cuda_dep.version()
endif

# FIXME: use break syntax when we use meson >= '0.49'
foreach v : cuda_versions
  if cuda_ver == ''
    cuda_dep = dependency('cuda-' + v, required: false)
    if cuda_dep.found()
      cuda_ver = v
    endif
  endif
endforeach

if cuda_dep.found()
  if cc.has_header('cuda.h', dependencies: cuda_dep)
    cuda_header_dep = cuda_dep.partial_dependency(compile_args : true, includes : true)
  endif
endif

if not cuda_header_dep.found()
  cuda_root = run_command(python3, '-c', 'import os; print(os.environ.get("CUDA_PATH"))').stdout().strip()
  if cuda_root != '' and cuda_root != 'None'
    cuda_incdir = join_paths (cuda_root, 'include')

    if cc.has_header('cuda.h', args: '-I' + cuda_incdir)
      cuda_header_dep = declare_dependency(include_directories: include_directories(cuda_incdir))
    endif
  endif
endif

if not cuda_header_dep.found()
  if nvcodec_option.enabled()
    error('The nvcodec plugin was enabled explicitly, but required CUDA dependency was not found.')
  endif
  subdir_done()
endif

if gstgl_dep.found()
  # FIXME: make nvdec usable without OpenGL dependency
  nvcodec_sources += nvdec_sources
  extra_c_args += ['-DHAVE_NVCODEC_GST_GL=1']
endif

gstnvcodec = library('gstnvcodec',
  nvcodec_sources,
  c_args : gst_plugins_bad_args + extra_c_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstpbutils_dep, gstgl_dep, cuda_header_dep, gmodule_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstnvcodec, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstnvcodec]

