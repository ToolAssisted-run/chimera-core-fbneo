# chimera-core-fbneo: FinalBurn Neo's arcade boards in miniBox's sandbox, as a
# Chimera core. Five systems: Capcom CPS-1, CPS-2 and CPS-3, SNK Neo Geo, and
# Sega System 16 (16A and 16B).
#
# Native reference build (run-native + run-wbx under the build dir):
#     meson setup build/meson-native && ninja -C build/meson-native
# Guest (core.wbx) build via the cross file waterbox/setup-guest.sh writes:
#     sh waterbox/setup-guest.sh && ninja -C build/meson-guest core.wbx
#
# THE EMULATOR IS UPSTREAM FBNEO. Its own meson fragments are used as they
# are: each driver group's fragment names the drivers and the CPUs, devices
# and sound chips each needs, and src/cpu, src/burn/devices and src/burn/snd
# build exactly the components the chosen drivers ask for. What upstream's
# root adds - a frontend (SDL, Win32, ...) and the executable around it - is
# the one part left out: the driver in waterbox/ is this core's frontend.
project('chimera-core-fbneo', ['c', 'cpp'], version: '0.1',
  default_options: ['c_std=gnu11', 'cpp_std=gnu++17', 'optimization=2', 'debug=false',
                    'warning_level=0', 'b_ndebug=true'])

fb = 'extern/FBNeo'

# the submodule pin is pristine upstream; overlay the chimera patch set
run_command(files('waterbox/apply-patches.sh'), check: true)

mb = get_option('minibox_dir')
if mb == ''
  mb_candidate = meson.current_source_dir() / '../chimera/extern/chimera-common-minibox'
  if import('fs').is_dir(mb_candidate)
    mb = mb_candidate
  else
    error('pass -Dminibox_dir=<miniBox checkout> (or keep one at ../chimera/extern/chimera-common-minibox)')
  endif
endif

perl = find_program('perl', required: true)
cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')
fs = import('fs')

# What upstream's root sets for its fragments. No frontend macro at all: the
# BUILD_<frontend> ones switch on runahead, FLAC/MP3 samples and wide-string
# titles, none of which a headless core wants. __fastcall is empty on x86-64,
# which is what upstream's own probe concludes there.
add_project_arguments('-D_GNU_SOURCE', '-D__fastcall=', '-DNDEBUG', language: ['c', 'cpp'])
add_project_arguments('-Wno-write-strings', '-Wno-narrowing', '-Wno-register', language: 'cpp')
# burn.cpp's title code takes a wide-string path when no frontend is named, and
# only the frontends include <wchar.h> for it
add_project_arguments('-include', 'wchar.h', language: 'cpp')
# FBNeo's bundled zlib: say unistd.h exists (musl's gz_open wants lseek from it)
add_project_arguments('-DZ_HAVE_UNISTD_H', language: 'c')
frontend = 'none'
frontend_defs = []
platform_math_defs = []
common_cpp_warning_args = ['-Wno-sequence-point', '-Wno-sign-compare',
  '-Wno-unused-but-set-variable', '-Wno-unused-parameter', '-Wno-unused-value',
  '-Wno-unused-variable']

global_inc = include_directories(
  fb / 'src/burn',
  fb / 'src/burn/devices',
  fb / 'src/burn/snd',
  fb / 'src/cpu',
  fb / 'src/burner',
  # tchar.h for a non-Windows build lives with the SDL frontend; nothing else
  # of that frontend is compiled
  fb / 'src/burner/sdl',
  fb / 'src/intf',
  fb / 'src/intf/audio',
  fb / 'src/intf/video',
  fb / 'src/intf/input',
  fb / 'src/intf/cd',
  fb / 'src/intf/perfcount',
)

# ---- the five systems ------------------------------------------------------
# group -> the driver files of it this core builds. d_parent.cpp (parent
# drivers of clones in other groups) is compiled by upstream's drv fragment.
selection = {
  'capcom': ['d_cps1.cpp', 'd_cps2.cpp'],
  'cps3': ['d_cps3.cpp'],
  'neogeo': ['d_neogeo.cpp'],
  'sega': ['d_sys16a.cpp', 'd_sys16b.cpp'],
}
# Compiled but not listed: Sega's shared board code (sys16_run.cpp) calls into
# the System 18, Hang-On, OutRun, X-Board and Y-Board drivers, so they must
# link - but they stay out of driverlist.h, so none of their games exists here.
support = {
  'sega': ['d_sys18.cpp', 'd_hangon.cpp', 'd_outrun.cpp', 'd_xbrd.cpp', 'd_ybrd.cpp'],
}

enabled_components = []
burn_drivers = {}
driver_files_abs = [meson.current_source_dir() / fb / 'src/burn/drv/d_parent.cpp']
foreach g, chosen : selection
  subdir(fb / 'src/burn/drv' / g)
  foreach d : chosen
    assert(drivers.has_key(d), 'Unknown driver ' + d + ' in group ' + g)
    driver_files_abs += meson.current_source_dir() / fb / 'src/burn/drv' / g / d
    foreach c : drivers[d]
      if not enabled_components.contains(c)
        enabled_components += c
      endif
    endforeach
  endforeach
  extra = support.get(g, [])
  foreach d : extra
    foreach c : drivers[d]
      if not enabled_components.contains(c)
        enabled_components += c
      endif
    endforeach
  endforeach
  burn_drivers += {g: {'desc': group_desc, 'drivers': chosen, 'srcs': group_srcs + extra}}
endforeach

subdir(fb / 'src/burn')
subdir(fb / 'src/cpu')
subdir(fb / 'src/burn/devices')
subdir(fb / 'src/burn/snd')
subdir(fb / 'src/burn/drv')
subdir(fb / 'src/dep/libs/zlib')

fbneo_deps = [burn_drv_dep, burn_dep, cpu_dep, burn_devices_dep, burn_snd_dep, zlib_dep]

inc = include_directories('waterbox')

driver_sources = files(
  'waterbox/fbneo-driver.cpp',
  'waterbox/frontend-stubs.cpp',
  'waterbox/zip-reader.cpp',
  'waterbox/wbx-entry.cpp',
)

if meson.is_cross_build()
  # ---- the guest: core.wbx --------------------------------------------------
  mbdir = meson.get_external_property('minibox_dir')
  mbuild = meson.get_external_property('minibox_build')
  sr = meson.get_external_property('guest_sysroot')

  executable('core', driver_sources, files('waterbox/guest-syscalls.cpp'),
    name_suffix: 'wbx',
    include_directories: [inc, global_inc],
    dependencies: fbneo_deps,
    link_args: [
      '-static', '-no-pie',
      '-Wl,--eh-frame-hdr', '-Wl,-O2', '-Wl,--no-relax', '-Wl,-z,stack-size=8388608',
      '-T', mbdir / 'source/guest/linkscript.T',
      '-Wl,-u,pthread_once', '-Wl,-u,pthread_cond_wait',
      '-Wl,-u,pthread_cond_broadcast', '-Wl,-u,pthread_key_create',
      mbuild / 'source/guest/cxxglue.c.o',
      mbuild / 'source/guest/emulibc.c.o',
      '-L' + sr / 'lib', '-lstdc++', '-lgcc', '-lgcc_eh', '-lc'])
else
  # ---- the native reference -------------------------------------------------
  m = cc.find_library('m', required: false)
  executable('run-native', driver_sources, files('waterbox/run-native.c'),
    include_directories: [include_directories('waterbox/native-shim'), inc, global_inc],
    c_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    cpp_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    dependencies: fbneo_deps + [m])

  mbhost = mb / 'build/meson-linux/source/host'
  executable('run-wbx', files('waterbox/run-wbx.c'),
    c_args: ['-I' + mb / 'source/host'],
    link_args: [mbhost / 'libminiboxhost.so', '-Wl,-rpath,' + mbhost])
endif
