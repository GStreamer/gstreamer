# Getting dependencies and building macros is done via SwiftPM
subdir('swiftpm')

cdata.set('HAVE_SCREENCAPTUREKIT', 1)

applemedia_swift_sources = [
  'sckitsrc/Bindings.swift',
  'sckitsrc/Macros.swift',
  'sckitsrc/Utils.swift',
  'sckitsrc/SCKitShared.swift',
  'sckitsrc/SCKitVideoSrc.swift',
  'c-bridge.h',
]

current_dir = meson.current_build_dir()
applemedia_swift_args = [
  '-Xfrontend', '-load-plugin-executable',
  '-Xfrontend', current_dir + '/swiftpm/libGstSwiftDeps.a.p/' + build_type + '/GstSwiftMacros#GstSwiftMacros',
  '-I' + current_dir + '/swiftpm/libGstSwiftDeps.a.p/' + build_type,
  # FIXME! (idea kinda borrowed from gst-editing-services)
  # How can we access the generated headers correctly? (enumtypes.h etc.)
  '-I' + meson.current_build_dir() + '/../../../../gstreamer',
  '-I' + meson.current_build_dir() + '/../../../../gstreamer/libs',
  '-I' + meson.current_build_dir() + '/../../../../gst-plugins-base',
  '-I' + meson.current_build_dir() + '/../../../../gst-plugins-base/gst-libs',
  '-emit-objc-header-path', meson.current_build_dir() + '/GstSCKitSrc-Swift.h',
]

xcodeselect = find_program('xcode-select')
xcode_path = run_command(xcodeselect, ['--print-path'], check: true).stdout().strip()

# libswift_Concurrency, needed for async support in Swift, is only present as a system lib on macOS 12.0+.
# We're only running code using it on 12.0+ anyway, but the whole build is targeting an older macOS
# version (10.13), which causes this lib to be linked with @rpath instead of directly to /usr/lib/swift.
# To work around this, we weak-link this particular lib and add an RPATH to the swift lib dir.
# Important: this relies on Meson 1.5.0+ to not remove this rpath upon installation.
gstapplemedia_link_args += [
  '-Wl,-rpath,/usr/lib/swift',
  # This one is needed for the linker to pick up Swift's compatibility libs.
  '-L' + xcode_path + '/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/macosx',
  # Weak link this one so the applemedia plugin won't fail on macOS <12 at runtime.
  '-weak-lswift_Concurrency',
]

gstapplemedia_swift = static_library('gstapplemediaswift',
  applemedia_swift_sources, applemedia_swiftpm_deps[1],
  dependencies: [gst_dep, gstbase_dep, gstvideo_dep],
  swift_args: applemedia_swift_args,
)
gstapplemedia_swiftinc += [include_directories('.')]

# Ensure that GstSCKitSrc-Swift.h is generated before any sckit-using
# Objective-C code is built
dephack_GstSCKitSrc_Swift_h = custom_target('dephack_GstSCKitSrc_Swift_h',
  output: 'GstSCKitSrc-Swift.h',
  depends: gstapplemedia_swift,
  depend_files: applemedia_swift_sources,
  command: ['touch', '-c', '@OUTPUT@'],
)
applemedia_sources += dephack_GstSCKitSrc_Swift_h

gstapplemedia_sckit = static_library('gstapplemedia_sckit',
  'sckitvideosrc.m', applemedia_swiftpm_deps[2], dephack_GstSCKitSrc_Swift_h,
  dependencies: [gst_dep, gstbase_dep, gstvideo_dep],
  objc_args: ['-mmacosx-version-min=12.3'])

gstapplemedia_libs += [applemedia_swiftpm_deps[0]]
gstapplemedia_whole_libs += [gstapplemedia_sckit, gstapplemedia_swift]
