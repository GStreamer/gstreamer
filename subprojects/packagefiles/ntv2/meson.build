project('ajantv2', ['c', 'cpp'],
  version : '17.0.1',
  meson_version : '>= 0.54.0',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized',
                      'c_std=c11',
                      'cpp_std=c++11',
                      'cpp_eh=none',
                      'cpp_rtti=false',
                    ]
)

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
    cxx.get_supported_arguments(['-Wno-deprecated-declarations']),
    language: 'cpp',
  )
endif

conf_data = configuration_data()
conf_data.set('AJA_NTV2_SDK_VERSION_MAJOR', '17')
conf_data.set('AJA_NTV2_SDK_VERSION_MINOR', '0')
conf_data.set('AJA_NTV2_SDK_VERSION_POINT', '1')
conf_data.set('AJA_NTV2_SDK_BUILD_NUMBER', '0')
conf_data.set('AJA_NTV2_SDK_BUILD_DATETIME', '2024-02-15T17:41:48-08:00')
conf_data.set('AJA_NTV2_SDK_BUILD_TYPE', '')
configure_file(input : 'ajantv2/includes/ntv2version.h.in',
               output : 'ntv2version.h',
               configuration : conf_data)

thread_dep = dependency('threads')
rt_dep = cxx.find_library('rt', required : false)
dl_dep = cxx.find_library('dl', required : false)

ajantv2_sources = [
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
  'ajabase/pnp/linux/pnpimpl.cpp',
  'ajabase/system/linux/eventimpl.cpp',
  'ajabase/system/linux/infoimpl.cpp',
  'ajabase/system/linux/lockimpl.cpp',
  'ajabase/system/linux/processimpl.cpp',
  'ajabase/system/linux/threadimpl.cpp',
  'ajantv2/src/ntv2anc.cpp',
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
  'ajantv2/src/ntv2hevc.cpp',
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
  'ajantv2/src/ntv2task.cpp',
  'ajantv2/src/ntv2testpatterngen.cpp',
  'ajantv2/src/ntv2transcode.cpp',
  'ajantv2/src/ntv2utf8.cpp',
  'ajantv2/src/ntv2utils.cpp',
  'ajantv2/src/ntv2version.cpp',
  'ajantv2/src/ntv2verticalfilter.cpp',
  'ajantv2/src/ntv2vpid.cpp',
  'ajantv2/src/ntv2vpidfromspec.cpp',
  'ajantv2/src/lin/ntv2linuxdriverinterface.cpp',
]

ajantv2_args = [
  '-DAJALinux',
  '-DAJASTATIC',
  '-DAJA_LINUX',
  '-DAJA_USE_CPLUSPLUS11',
  '-DNDEBUG',
  '-DNTV2_USE_CPLUSPLUS11',
]

ajantv2_inc = include_directories(
  '.',
  'ajaanc/includes',
  'ajantv2/includes',
  'ajantv2/src/lin',
)

libajantv2 = static_library(
  'libajantv2',
  sources: ajantv2_sources,
  dependencies : [thread_dep, rt_dep, dl_dep],
  cpp_args: [ajantv2_args, common_flags],
  include_directories: ajantv2_inc,
  pic: true,
  override_options: ['cpp_eh=default', 'werror=false'],
  install: false
)

libajantv2_dep = declare_dependency(
  compile_args : ajantv2_args,
  link_with: libajantv2,
  include_directories: ajantv2_inc,
)
