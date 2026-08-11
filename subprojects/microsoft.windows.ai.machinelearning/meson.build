project('microsoft.windows.ai.machinelearning', 'c', 'cpp',
  version : '2.2.12')

py3 = import('python3').find_python()

if host_machine.system() != 'windows'
  error('Can only download Windows ML for Windows')
endif

message('Downloading and extracting Microsoft.Windows.AI.MachineLearning nuget package')

arch = target_machine.cpu_family()
if arch not in ['x86_64', 'aarch64']
  error(f'Unexpected architecture @arch@')
endif

zip_hash = '9cb60543337e6e4eac2a95c2fcb9650a880697ae7190d15499d801c907849da3'
version = meson.project_version()
ret = run_command(py3, files('download-binary.py'), version, zip_hash,
    check: true)

base_path = f'winml-@version@'

winml_inc_dir = include_directories(base_path / 'include')
onnxruntime_inc_dir = include_directories(base_path / 'include' / 'winml')

lib_base_path = meson.current_source_dir() / base_path / 'lib' / 'native'
lib_path = ''
runtime_path = ''

if arch == 'x86_64'
  lib_path = lib_base_path / 'x64'
  runtime_path = meson.current_source_dir() / base_path / 'runtimes' / 'win-x64' / 'native'
else
  lib_path = lib_base_path / 'arm64'
  runtime_path = meson.current_source_dir() / base_path / 'runtimes' / 'win-arm64' / 'native'
endif

cc = meson.get_compiler('c')

onnx_import_lib = cc.find_library(
  'onnxruntime',
  dirs: lib_path,
)

winml_import_lib = cc.find_library(
  'Microsoft.Windows.AI.MachineLearning',
  dirs: lib_path,
)

windows_ml_api_dep = declare_dependency(
  include_directories: winml_inc_dir,
  dependencies: winml_import_lib,
)

onnxruntime_dep = declare_dependency(
  include_directories: onnxruntime_inc_dir,
  dependencies: onnx_import_lib,
)

windows_ml_dep = declare_dependency(
  dependencies: [
    windows_ml_api_dep,
    onnxruntime_dep,
  ],
)

meson.override_dependency(
  'microsoft.windows.ai.machinelearning',
  windows_ml_dep,
)

pkgconfig = import('pkgconfig')
pkgconfig.generate(
  name: 'Microsoft Windows AI Machine Learning',
  filebase: 'microsoft.windows.ai.machinelearning',
  description: 'Microsoft Windows AI Machine Learning',
  version: meson.project_version(),
  libraries: [
    winml_import_lib,
    onnx_import_lib,
  ],
  subdirs: [
    '.',
    'winml',
  ],
)

install_subdir(
  base_path / 'include',
  install_dir: get_option('includedir'),
  strip_directory: true,
)

install_data(
  lib_path / 'onnxruntime.lib',
  lib_path / 'Microsoft.Windows.AI.MachineLearning.lib',
  install_dir: get_option('libdir'),
)

install_data(
  runtime_path / 'Microsoft.Windows.AI.MachineLearning.dll',
  runtime_path / 'onnxruntime.dll',
  runtime_path / 'DirectML.dll',
  install_dir: get_option('bindir'),
)

devenv = environment()
devenv.prepend('PATH', runtime_path)
meson.add_devenv(devenv)
