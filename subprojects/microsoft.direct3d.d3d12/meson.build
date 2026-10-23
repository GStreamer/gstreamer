project('microsoft.direct3d.d3d12', version : '1.619.6')

if host_machine.system() != 'windows'
  error('Can only download the DirectX Agility SDK for Windows')
endif

arch = host_machine.cpu_family()
if arch == 'x86_64'
  runtime_arch = 'x64'
elif arch == 'x86'
  runtime_arch = 'win32'
elif arch == 'aarch64'
  runtime_arch = 'arm64'
else
  error(f'Unexpected architecture @arch@')
endif

py3 = import('python3').find_python()

message('Downloading and extracting Microsoft.Direct3D.D3D12 nuget package')

zip_hash = '08f0489281401aa430fc37322d6c3fc98a8025175aacd714c10d562f4963f1e9'
version = meson.project_version()
run_command(py3, files('download-binary.py'), version, zip_hash, check: true)

base_path = f'agility-sdk-@version@'
runtime_path = meson.current_source_dir() / base_path / 'build' / 'native' / 'bin' / runtime_arch
sdk_version = 619

# Keep using DirectX-Headers for compilation.
agility_sdk_dep = declare_dependency(
  version: version,
)

meson.override_dependency('microsoft.direct3d.d3d12', agility_sdk_dep)

devenv = environment()
devenv.set('GST_D3D12_AGILITY_SDK_PATH', runtime_path)
devenv.set('GST_D3D12_AGILITY_SDK_VERSION', sdk_version.to_string())
meson.add_devenv(devenv)
