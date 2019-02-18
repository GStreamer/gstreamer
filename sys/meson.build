subdir('androidmedia')
subdir('applemedia')
subdir('bluez')
subdir('d3dvideosink')
subdir('decklink')
subdir('directsound')
#subdir('dshowdecwrapper')
#subdir('dshowsrcwrapper')
#subdir('dshowvideosink')
subdir('dvb')
subdir('fbdev')
subdir('ipcpipeline')
subdir('kms')
subdir('msdk')
subdir('opensles')
subdir('shm')
subdir('tinyalsa')
subdir('uvch264')
subdir('vdpau')
subdir('wasapi')
subdir('winks')
subdir('winscreencap')

# CUDA dependency
cuda_dep = dependency('', required : false)
cudart_dep = dependency('', required : false)
cuda_libdir = ''
cuda_incdir = ''

if host_machine.system() == 'windows'
  # On windows, CUDA_PATH env will be set by installer
  cuda_root = run_command(python3, '-c', 'import os; print(os.environ.get("CUDA_PATH"))').stdout().strip()
  if cuda_root != '' and cuda_root != 'None'
    arc = ''
    if build_machine.cpu_family() == 'x86_64'
      arc = 'x64'
    else
      arc = 'Win32'
    endif
    cuda_libdir = join_paths (cuda_root, 'lib', arc)
    cuda_incdir = join_paths (cuda_root, 'include')
    cuda_lib = cc.find_library('cuda', dirs: cuda_libdir, required: false)
    cudart_lib = cc.find_library('cudart', dirs: cuda_libdir, required: false)
    if cuda_lib.found()
      cuda_header_found = cc.has_header('cuda.h', args: '-I' + cuda_incdir)
      cuda_lib_found = cc.has_function('cuInit', dependencies: cuda_lib)
      if cuda_header_found and cuda_lib_found
        cuda_dep = declare_dependency(include_directories: include_directories(cuda_incdir),
                                      dependencies: cuda_lib)
      endif
    endif

    if cudart_lib.found()
      cudart_header_found = cc.has_header('cuda_runtime_api.h', args: '-I' + cuda_incdir)
      cudart_lib_found = cc.has_function('cudaGetErrorString', dependencies: cudart_lib)
      if cudart_header_found and cudart_lib_found
        cudart_dep = declare_dependency(dependencies: cudart_lib)
      endif
    endif
  endif
else
  cuda_versions = [
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

  # FIXME: use break syntax when we use meson >= '0.49'
  foreach v : cuda_versions
    if cuda_ver == ''
      cuda_dep = dependency('cuda-' + v, required: false)
      cudart_dep = dependency('cudart-' + v, required: false)
      if cuda_dep.found() and cudart_dep.found()
        cuda_ver = v
      endif
    endif
  endforeach

  if cuda_dep.found()
    cuda_header_found = cc.has_header('cuda.h', dependencies: cuda_dep)
    cuda_lib_found = cc.has_function('cuInit', dependencies: cuda_dep)
    if not cuda_header_found or not cuda_lib_found
      message ('Missing required header and/or function in cuda dependency')
      cuda_dep = dependency('', required : false)
    endif
  endif

  if cudart_dep.found()
    cudart_header_found = cc.has_header('cuda_runtime_api.h', dependencies: cudart_dep)
    cudart_lib_found = cc.has_function('cudaGetErrorString', dependencies: cudart_dep)
    if not cudart_header_found or not cudart_lib_found
      message ('Missing required header and/or function in cudart dependency')
      cudart_dep = dependency('', required : false)
    endif
  endif
endif

if cuda_dep.found() and cudart_dep.found()
  subdir('nvdec')
  subdir('nvenc')
elif get_option('nvdec').enabled()
  error('The nvdec plugin was enabled explicitly, but required CUDA dependencies were not found.')
elif get_option('nvenc').enabled()
  error('The nvenc plugin was enabled explicitly, but required CUDA dependencies were not found.')
endif
