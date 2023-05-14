project('webview2', version : '1.0.2420.47')

py3 = import('python3').find_python()

if host_machine.system() != 'windows'
  error('Can only download webview2 for Windows')
endif

message('Downloading and extracting webview2 nuget package')

arch = target_machine.cpu_family()
if arch != 'x86_64' and arch != 'x86' and arch != 'aarch64'
  error('Unexpected architecture' + arch)
endif

zip_hash = '8e5a7307d71507edbbe02cac27215d71058bbd82cd256cef60f06b945907610a'
version = meson.project_version()
ret = run_command(py3, files('download-binary.py'), meson.project_version(), zip_hash,
    check: true)

base_path = join_paths(meson.current_source_dir(), 'webview2-@0@'.format(version),
    'build', 'native')
inc_dir = include_directories(join_paths(base_path, 'include'))
lib_path = ''
if arch == 'x86_64'
  lib_path = join_paths(base_path, 'x64')
elif arch == 'x86'
  lib_path = join_paths(base_path, 'x86')
else
  lib_path = join_paths(base_path, 'arm64')
endif

cc = meson.get_compiler('c')
loader_static = cc.find_library('WebView2LoaderStatic', dirs: [lib_path])
webview2_dep = declare_dependency(include_directories: inc_dir,
    dependencies: loader_static)
