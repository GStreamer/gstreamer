applemedia_swiftpm_sources = files(
  'Package.swift',
  'Sources/GstSwiftMacros/GstSwiftMacros.swift',
  'Sources/GstSwiftMacros/GstDebugMacros.swift',
)

arch = host_machine.cpu_family()
subsystem = host_machine.subsystem()
# SCKit is only available on macOS 12.3+
assert(subsystem == 'macos')
os_ver = '12.3'

if get_option('debug')
  build_type = 'debug'
else
  build_type = 'release'
endif

# This retrieves external SwiftPM 'deps we use (swift-collections, macros)
# and builds them into a static library, which we can then use for our Meson-only build.
applemedia_swiftpm_deps = custom_target('applemedia-swiftpm-deps',
  output: ['libGstSwiftDeps.a', 'dephack.swift', 'dephack.h'],
  depend_files: applemedia_swiftpm_sources,
  command: [
    swiftpm, 'build',
    '--triple', f'@arch@-apple-@subsystem@@os_ver@',
    '--configuration', build_type,
    '--package-path', meson.current_source_dir(),      # package path = working source dir
    '--build-path', '@PRIVATE_DIR@',                   # build path = output dir
    '&&',
    'cp', '@PRIVATE_DIR@/' + build_type + '/libGstSwiftDeps.a', '@OUTPUT0@',
    '&&',
    'touch', '@OUTPUT1@', '@OUTPUT2@',
  ],
)
