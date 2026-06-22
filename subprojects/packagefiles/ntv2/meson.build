project('ajantv2', ['c', 'cpp'],
  version : '18.0.0',
  meson_version : '>= 1.3.0',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized',
                      'c_std=c11',
                      'cpp_std=c++11',
                      'cpp_eh=none',
                      'cpp_rtti=false',
                    ]
)

if host_machine.system() in ['darwin', 'ios']
  add_languages('objc', 'objcpp', native: false)
endif

c = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')
test_cppflags = ['-Wno-non-virtual-dtor', '-Wno-overloaded-virtual']

common_flags = []
foreach cxxflag: test_cppflags
  if cxx.has_argument(cxxflag)
    common_flags += [ cxxflag ]
  endif
endforeach

if get_option('warning_level') == '0'
  add_project_arguments(
    cxx.get_supported_arguments(['-Wno-deprecated-declarations', '-Wno-return-local-addr']),
    language: 'cpp',
  )
endif

if cxx.get_argument_syntax() == 'msvc'
  # Meson force enables /permissive- whenever using
  # cpp_std=c++xx (and not vc++xx).
  # This causes: 'subprojects/libajantv2-ntv2_18_0_0/ajabase/system/windows/infoimpl.cpp(238): error C2362: "goto cleanup" omitió la inicialización de ''uReturn'
  # So work around it with the specific VS 2022+ flag, or outright
  # disable strict conformance.
  if cxx.has_argument('/Zc:gotoScope-')
    common_flags += '/Zc:gotoScope-'
  else
    common_flags += '/permissive'
  endif
endif

conf_data = configuration_data()
conf_data.set('AJA_NTV2_SDK_VERSION_MAJOR', '18')
conf_data.set('AJA_NTV2_SDK_VERSION_MINOR', '1')
conf_data.set('AJA_NTV2_SDK_VERSION_POINT', '0')
conf_data.set('AJA_NTV2_SDK_BUILD_NUMBER', '0')
conf_data.set('AJA_NTV2_SDK_BUILD_DATETIME', '2026-06-19T22:57:37Z')
conf_data.set('AJA_NTV2_SDK_BUILD_TYPE', '')
ntv2version_h = configure_file(input : 'ajantv2/includes/ntv2version.h.in',
                               output : 'ntv2version.h',
                               configuration : conf_data)

ajantv2_deps = [
  dependency('threads'),
]

if host_machine.system() == 'linux'
  ajantv2_deps += [
    cxx.find_library('rt', required : false),
    cxx.find_library('dl', required : false),
    dependency('libudev'),
  ]
elif host_machine.system() == 'darwin'
  ajantv2_deps += [
    dependency('appleframeworks', modules : ['Foundation', 'CoreFoundation', 'IOKit']),
  ]
elif host_machine.system() == 'windows'
  ajantv2_deps += [
    cxx.find_library('advapi32'),
    cxx.find_library('comctl32'),
    cxx.find_library('netapi32'),
    cxx.find_library('odbc32'),
    cxx.find_library('psapi'),
    cxx.find_library('rpcrt4'),
    cxx.find_library('setupapi'),
    cxx.find_library('shell32'),
    cxx.find_library('shlwapi'),
    cxx.find_library('user32'),
    cxx.find_library('wbemuuid'),
    cxx.find_library('winmm'),
    cxx.find_library('ws2_32'),
    cxx.find_library('wsock32'),
    cxx.find_library('bcrypt'),
  ]
endif

ajantv2_headers = files(
  'ajaanc/includes/ancillarydata.h',
  'ajaanc/includes/ancillarydatafactory.h',
  'ajaanc/includes/ancillarydata_cea608.h',
  'ajaanc/includes/ancillarydata_cea608_line21.h',
  'ajaanc/includes/ancillarydata_cea608_vanc.h',
  'ajaanc/includes/ancillarydata_cea708.h',
  'ajaanc/includes/ancillarydata_framestatusinfo524D.h',
  'ajaanc/includes/ancillarydata_framestatusinfo5251.h',
  'ajaanc/includes/ancillarydata_hdr_hdr10.h',
  'ajaanc/includes/ancillarydata_hdr_hlg.h',
  'ajaanc/includes/ancillarydata_hdr_sdr.h',
  'ajaanc/includes/ancillarydata_timecode.h',
  'ajaanc/includes/ancillarydata_timecode_atc.h',
  'ajaanc/includes/ancillarydata_timecode_vitc.h',
  'ajaanc/includes/ancillarydata_hdmi_aux.h',
  'ajaanc/includes/ancillarylist.h',
  'ajabase/common/ajamovingavg.h',
  'ajabase/common/ajarefptr.h',
  'ajabase/common/audioutilities.h',
  'ajabase/common/buffer.h',
  'ajabase/common/bytestream.h',
  'ajabase/common/circularbuffer.h',
  'ajabase/common/cli_utils.h',
  'ajabase/common/commandline.h',
  'ajabase/common/common.h',
  'ajabase/common/dpxfileio.h',
  'ajabase/common/dpx_hdr.h',
  'ajabase/common/export.h',
  'ajabase/common/guid.h',
  'ajabase/common/options_popt.h',
  'ajabase/common/performance.h',
  'ajabase/common/pixelformat.h',
  'ajabase/common/public.h',
  'ajabase/common/rawfile.h',
  'ajabase/common/timebase.h',
  'ajabase/common/timecode.h',
  'ajabase/common/timecodeburn.h',
  'ajabase/common/timer.h',
  'ajabase/common/types.h',
  'ajabase/common/variant.h',
  'ajabase/common/videotypes.h',
  'ajabase/common/videoutilities.h',
  'ajabase/common/wavewriter.h',
  'ajabase/network/ip_socket.h',
  'ajabase/network/tcp_socket.h',
  'ajabase/network/udp_socket.h',
  'ajabase/persistence/persistence.h',
  'ajabase/persistence/sqlite3.h',
  'ajabase/pnp/pnp.h',
  'ajabase/system/atomic.h',
  'ajabase/system/debug.h',
  'ajabase/system/debugshare.h',
  'ajabase/system/diskstatus.h',
  'ajabase/system/event.h',
  'ajabase/system/file_io.h',
  'ajabase/system/info.h',
  'ajabase/system/lock.h',
  'ajabase/system/log.h',
  'ajabase/system/make_unique_shim.h',
  'ajabase/system/memory.h',
  'ajabase/system/process.h',
  'ajabase/system/system.h',
  'ajabase/system/systemtime.h',
  'ajabase/system/thread.h',
  'ajantv2/includes/ajaexport.h',
  'ajantv2/includes/ajatypes.h',
  'ajantv2/includes/basemachinecontrol.h',
  'ajantv2/includes/ntv2audiodefines.h',
  'ajantv2/includes/ntv2bft.h',
  'ajantv2/includes/ntv2bitfile.h',
  'ajantv2/includes/ntv2bitfilemanager.h',
  'ajantv2/includes/ntv2card.h',
  'ajantv2/includes/ntv2choosableboard.h',
  'ajantv2/includes/ntv2config2022.h',
  'ajantv2/includes/ntv2config2110.h',
  'ajantv2/includes/ntv2configts2022.h',
  'ajantv2/includes/ntv2cscmatrix.h',
  'ajantv2/includes/ntv2debug.h',
  'ajantv2/includes/ntv2debugmacros.h',
  'ajantv2/includes/ntv2deprecate.h',
  'ajantv2/includes/ntv2devicecapabilities.h',
  'ajantv2/includes/ntv2devicefeatures.h',
  'ajantv2/includes/ntv2devicefeatures.hh',
  'ajantv2/includes/ntv2devicescanner.h',
  'ajantv2/includes/ntv2driverinterface.h',
  'ajantv2/includes/ntv2endian.h',
  'ajantv2/includes/ntv2enhancedcsc.h',
  'ajantv2/includes/ntv2enums.h',
  'ajantv2/includes/ntv2fixed.h',
  'ajantv2/includes/ntv2formatdescriptor.h',
  'ajantv2/includes/ntv2konaflashprogram.h',
  'ajantv2/includes/ntv2m31enums.h',
  'ajantv2/includes/ntv2m31publicinterface.h',
  'ajantv2/includes/ntv2mailbox.h',
  'ajantv2/includes/ntv2mbcontroller.h',
  'ajantv2/includes/ntv2mcsfile.h',
  'ajantv2/includes/ntv2nubaccess.h',
  'ajantv2/includes/ntv2nubtypes.h',
  'ajantv2/includes/ntv2publicinterface.h',
  'ajantv2/includes/ntv2registerexpert.h',
  'ajantv2/includes/ntv2registers2022.h',
  'ajantv2/includes/ntv2registers2110.h',
  'ajantv2/includes/ntv2registersmb.h',
  'ajantv2/includes/ntv2resample.h',
  'ajantv2/includes/ntv2routingexpert.h',
  'ajantv2/includes/ntv2rp188.h',
  'ajantv2/includes/ntv2serialcontrol.h',
  'ajantv2/includes/ntv2signalrouter.h',
  'ajantv2/includes/ntv2spiinterface.h',
  'ajantv2/includes/ntv2supportlogger.h',
  'ajantv2/includes/ntv2task.h',
  'ajantv2/includes/ntv2testpatterngen.h',
  'ajantv2/includes/ntv2transcode.h',
  'ajantv2/includes/ntv2tshelper.h',
  'ajantv2/includes/ntv2utils.h',
  'ajantv2/includes/ntv2verticalfilter.h',
  'ajantv2/includes/ntv2videodefines.h',
  'ajantv2/includes/ntv2virtualregisters.h',
  'ajantv2/includes/ntv2vpid.h',
  'ajantv2/includes/ntv2vpidfromspec.h',
  'ajantv2/includes/ntv2link.h',
  'ajantv2/utilityfiles/public/ntv2firmwareinstallerthread.h',
)

ajantv2_sources = files(
  'ajaanc/src/ancillarydata.cpp',
  'ajaanc/src/ancillarydatafactory.cpp',
  'ajaanc/src/ancillarydata_cea608.cpp',
  'ajaanc/src/ancillarydata_cea608_line21.cpp',
  'ajaanc/src/ancillarydata_cea608_vanc.cpp',
  'ajaanc/src/ancillarydata_cea708.cpp',
  'ajaanc/src/ancillarydata_framestatusinfo524D.cpp',
  'ajaanc/src/ancillarydata_framestatusinfo5251.cpp',
  'ajaanc/src/ancillarydata_hdr_hdr10.cpp',
  'ajaanc/src/ancillarydata_hdr_hlg.cpp',
  'ajaanc/src/ancillarydata_hdr_sdr.cpp',
  'ajaanc/src/ancillarydata_timecode.cpp',
  'ajaanc/src/ancillarydata_timecode_atc.cpp',
  'ajaanc/src/ancillarydata_timecode_vitc.cpp',
  'ajaanc/src/ancillarydata_hdmi_aux.cpp',
  'ajaanc/src/ancillarylist.cpp',
  'ajabase/common/audioutilities.cpp',
  'ajabase/common/buffer.cpp',
  'ajabase/common/commandline.cpp',
  'ajabase/common/common.cpp',
  'ajabase/common/dpxfileio.cpp',
  'ajabase/common/dpx_hdr.cpp',
  'ajabase/common/guid.cpp',
  'ajabase/common/options_popt.cpp',
  'ajabase/common/performance.cpp',
  'ajabase/common/pixelformat.cpp',
  'ajabase/common/timebase.cpp',
  'ajabase/common/timecode.cpp',
  'ajabase/common/timecodeburn.cpp',
  'ajabase/common/timer.cpp',
  'ajabase/common/variant.cpp',
  'ajabase/common/videoutilities.cpp',
  'ajabase/common/wavewriter.cpp',
  'ajabase/network/ip_socket.cpp',
  'ajabase/network/network.cpp',
  'ajabase/network/tcp_socket.cpp',
  'ajabase/network/udp_socket.cpp',
  'ajabase/persistence/persistence.cpp',
  'ajabase/persistence/sqlite3.c',
  'ajabase/pnp/pnp.cpp',
  'ajabase/system/atomic.cpp',
  'ajabase/system/debug.cpp',
  'ajabase/system/diskstatus.cpp',
  'ajabase/system/event.cpp',
  'ajabase/system/file_io.cpp',
  'ajabase/system/info.cpp',
  'ajabase/system/lock.cpp',
  'ajabase/system/log.cpp',
  'ajabase/system/memory.cpp',
  'ajabase/system/process.cpp',
  'ajabase/system/system.cpp',
  'ajabase/system/systemtime.cpp',
  'ajabase/system/thread.cpp',
  'ajantv2/src/ntv2anc.cpp',
  'ajantv2/src/ntv2aux.cpp',
  'ajantv2/src/ntv2audio.cpp',
  'ajantv2/src/ntv2autocirculate.cpp',
  'ajantv2/src/ntv2bitfile.cpp',
  'ajantv2/src/ntv2bitfilemanager.cpp',
  'ajantv2/src/ntv2card.cpp',
  'ajantv2/src/ntv2config2022.cpp',
  'ajantv2/src/ntv2config2110.cpp',
  'ajantv2/src/ntv2configts2022.cpp',
  'ajantv2/src/ntv2csclut.cpp',
  'ajantv2/src/ntv2cscmatrix.cpp',
  'ajantv2/src/ntv2debug.cpp',
  'ajantv2/src/ntv2devicefeatures.cpp',
  'ajantv2/src/ntv2devicefeatures.hpp',
  'ajantv2/src/ntv2devicescanner.cpp',
  'ajantv2/src/ntv2dma.cpp',
  'ajantv2/src/ntv2driverinterface.cpp',
  'ajantv2/src/ntv2dynamicdevice.cpp',
  'ajantv2/src/ntv2enhancedcsc.cpp',
  'ajantv2/src/ntv2formatdescriptor.cpp',
  'ajantv2/src/ntv2hdmi.cpp',
  'ajantv2/src/ntv2interrupts.cpp',
  'ajantv2/src/ntv2konaflashprogram.cpp',
  'ajantv2/src/ntv2mailbox.cpp',
  'ajantv2/src/ntv2mbcontroller.cpp',
  'ajantv2/src/ntv2mcsfile.cpp',
  'ajantv2/src/ntv2nubaccess.cpp',
  'ajantv2/src/ntv2publicinterface.cpp',
  'ajantv2/src/ntv2regconv.cpp',
  'ajantv2/src/ntv2register.cpp',
  'ajantv2/src/ntv2registerexpert.cpp',
  'ajantv2/src/ntv2regroute.cpp',
  'ajantv2/src/ntv2regvpid.cpp',
  'ajantv2/src/ntv2resample.cpp',
  'ajantv2/src/ntv2routingexpert.cpp',
  'ajantv2/src/ntv2rp188.cpp',
  'ajantv2/src/ntv2serialcontrol.cpp',
  'ajantv2/src/ntv2signalrouter.cpp',
  'ajantv2/src/ntv2spiinterface.cpp',
  'ajantv2/src/ntv2stream.cpp',
  'ajantv2/src/ntv2subscriptions.cpp',
  'ajantv2/src/ntv2supportlogger.cpp',
  'ajantv2/src/ntv2testpatterngen.cpp',
  'ajantv2/src/ntv2transcode.cpp',
  'ajantv2/src/ntv2utils.cpp',
  'ajantv2/src/ntv2version.cpp',
  'ajantv2/src/ntv2verticalfilter.cpp',
  'ajantv2/src/ntv2vpid.cpp',
  'ajantv2/src/ntv2vpidfromspec.cpp',
  'ajantv2/src/ntv2link.cpp',
)

ajantv2_headers_lin = files(
  'ajantv2/src/lin/ntv2linuxdriverinterface.h',
  'ajantv2/src/lin/ntv2linuxpublicinterface.h',
  'ajabase/pnp/linux/pnpimpl.h',
  'ajabase/system/linux/eventimpl.h',
  'ajabase/system/linux/infoimpl.h',
  'ajabase/system/linux/lockimpl.h',
  'ajabase/system/linux/processimpl.h',
  'ajabase/system/linux/threadimpl.h',
)

ajantv2_sources_lin = files(
  'ajabase/pnp/linux/pnpimpl.cpp',
  'ajabase/system/linux/eventimpl.cpp',
  'ajabase/system/linux/infoimpl.cpp',
  'ajabase/system/linux/lockimpl.cpp',
  'ajabase/system/linux/processimpl.cpp',
  'ajabase/system/linux/threadimpl.cpp',
  'ajantv2/src/lin/ntv2linuxdriverinterface.cpp',
)

ajantv2_headers_win = files(
  'ajantv2/src/win/ntv2windriverinterface.h',
  'ajantv2/src/win/ntv2winpublicinterface.h',
  'ajabase/pnp/windows/pnpimpl.h',
  'ajabase/system/windows/eventimpl.h',
  'ajabase/system/windows/infoimpl.h',
  'ajabase/system/windows/lockimpl.h',
  'ajabase/system/windows/processimpl.h',
  'ajabase/system/windows/threadimpl.h',
)

ajantv2_sources_win = files(
  'ajabase/pnp/windows/pnpimpl.cpp',
  'ajabase/system/windows/eventimpl.cpp',
  'ajabase/system/windows/infoimpl.cpp',
  'ajabase/system/windows/lockimpl.cpp',
  'ajabase/system/windows/processimpl.cpp',
  'ajabase/system/windows/threadimpl.cpp',
  'ajantv2/src/win/ntv2windriverinterface.cpp',
)

ajantv2_headers_mac = files(
  'ajantv2/src/mac/ntv2macdriverinterface.h',
  'ajantv2/src/mac/ntv2macpublicinterface.h',
  'ajabase/pnp/mac/devicenotifier.h',
  'ajabase/pnp/mac/masterport.h',
  'ajabase/pnp/mac/pnpimpl.h',
  'ajabase/system/mac/eventimpl.h',
  'ajabase/system/mac/infoimpl.h',
  'ajabase/system/mac/lockimpl.h',
  'ajabase/system/mac/processimpl.h',
  'ajabase/system/mac/pthreadsextra.h',
  'ajabase/system/mac/threadimpl.h',
)

ajantv2_sources_mac = files(
  'ajabase/pnp/mac/devicenotifier.cpp',
  'ajabase/pnp/mac/masterport.cpp',
  'ajabase/pnp/mac/pnpimpl.cpp',
  'ajabase/system/mac/eventimpl.cpp',
  'ajabase/system/mac/infoimpl.mm',
  'ajabase/system/mac/lockimpl.cpp',
  'ajabase/system/mac/processimpl.cpp',
  'ajabase/system/mac/pthreadsextra.cpp',
  'ajabase/system/mac/threadimpl.cpp',
  'ajantv2/src/mac/ntv2macdriverinterface.cpp',
)

ajantv2_args = [
  '-DAJASTATIC',
  '-DAJA_USE_CPLUSPLUS11',
  '-DNTV2_USE_CPLUSPLUS11',
  '-DNTV2_PREVENT_PLUGIN_LOAD',
]

ajantv2_inc = [include_directories(
  '.',
  'ajaanc/includes',
  'ajabase',
  'ajantv2/includes',
)]

install_inc = ['libajantv2', 'libajantv2/ajantv2/includes']

if host_machine.system() == 'linux'
  ajantv2_inc += include_directories(
    'ajantv2/src/lin',
  )
  ajantv2_sources += ajantv2_sources_lin
  ajantv2_headers += ajantv2_headers_lin
  ajantv2_args += [
    '-DAJALinux',
    '-DAJA_LINUX',
  ]
  install_inc += 'libajantv2/ajantv2/src/lin'
elif host_machine.system() == 'darwin'
  ajantv2_inc += include_directories(
    'ajantv2/src/mac',
  )
  ajantv2_sources += ajantv2_sources_mac
  ajantv2_headers += ajantv2_headers_mac
  ajantv2_args += [
    '-DAJAMac',
    '-DAJA_MAC',
    '-D__STDC_CONSTANT_MACROS',
  ]
  install_inc += 'libajantv2/ajantv2/src/mac'
elif host_machine.system() == 'windows'
  ajantv2_inc += include_directories(
    'ajantv2/src/win',
  )
  ajantv2_sources += ajantv2_sources_win
  ajantv2_headers += ajantv2_headers_win
  ajantv2_args += [
    '-DAJA_WINDOWS',
    '-DMSWindows',
    '-D_WINDOWS',
    '-D_CONSOLE',
    '-DWIN32_LEAN_AND_MEAN',
    '-D_CRT_SECURE_NO_WARNINGS',
    '-D_SCL_SECURE_NO_WARNINGS',
  ]
  common_flags += [
    '-D_MBCS',
    '-DCRT_NONSTDC_DEPRECATE',
    '-D_WINSOCK_DEPRECATED_NO_WARNINGS',
    '-DAJA_NO_AUTOIMPORT',
  ]
  install_inc += 'libajantv2/ajantv2/src/win'
endif

if get_option('debug') and get_option('optimization') == '0'
  ajantv2_args += [
    '-DAJA_DEBUG',
    '-D_DEBUG',
  ]
else
  ajantv2_args += [
    '-DNDEBUG',
  ]
endif

libajantv2 = static_library(
  'ajantv2',
  sources: ajantv2_sources,
  dependencies : ajantv2_deps,
  cpp_args: [ajantv2_args, common_flags],
  include_directories: ajantv2_inc,
  pic: true,
  override_options: ['cpp_eh=default', 'werror=false'],
  install: true
)

libajantv2_dep = declare_dependency(
  compile_args : ajantv2_args,
  link_with: libajantv2,
  include_directories: ajantv2_inc,
  dependencies: ajantv2_deps,
)

meson.override_dependency('libajantv2', libajantv2_dep)

install_headers(ajantv2_headers,
  subdir: 'libajantv2',
  preserve_path: true
)

install_headers(ntv2version_h, subdir: 'libajantv2/ajantv2/includes')

pkg = import('pkgconfig')

pkg.generate(
  libajantv2,
  filebase: 'libajantv2',
  extra_cflags: ajantv2_args,
  subdirs: install_inc,
)
