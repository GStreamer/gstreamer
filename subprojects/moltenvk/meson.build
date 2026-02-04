project('moltenvk', 'c', version : '1.4.341.0', meson_version: '>=1.2')
sha256 = '3b8af7f7db74b4bda02dc91503cec288ab70aebfe273ff98bb8cebabf04b196e'

host_system = host_machine.system()
if host_system != 'darwin'
  error('Only Darwin is supported at present')
endif
subsystem = host_machine.subsystem()

sdk_dir = 'sdk-' + meson.project_version() / 'macOS'
xcframework = sdk_dir / 'lib/MoltenVK.xcframework'
if subsystem == 'macos'
  libdir = sdk_dir / 'lib'
elif subsystem == 'ios'
  libdir = xcframework / 'ios-arm64'
elif subsystem == 'ios-simulator'
  libdir = xcframework / 'ios-arm64_x86_64-simulator'
elif subsystem == 'tvos'
  libdir = xcframework / 'tvos-arm64_arm64e'
elif subsystem == 'tvos-simulator'
  libdir = xcframework / 'tvos-arm64_x86_64-simulator'
else
  error(f'MoltenVK does not support @subsystem@')
endif

downloader = find_program('download-binary.py')
message('Downloading and extracting MoltenVK binaries for Windows...')
ret = run_command(downloader, meson.project_version(), host_system, sha256, check: true)

cc = meson.get_compiler('c')
vulkan_dep = declare_dependency(
  include_directories: include_directories(sdk_dir / 'include'),
  dependencies: [
    cc.find_library('MoltenVK', dirs: meson.current_source_dir() / libdir),
  ]
)
meson.override_find_program('glslc', files(sdk_dir / 'bin/glslc'))
meson.override_find_program('glslang', files(sdk_dir / 'bin/glslang'))
meson.override_dependency('MoltenVK', vulkan_dep, native: false, static: true)
