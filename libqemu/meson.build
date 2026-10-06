system_ss.add(files('libqemu.c', 'core.c'))
install_headers('libqemu.h', subdir: 'libqemu')
install_headers('compose.h', subdir: 'libqemu')

libqemu_compose = static_library('qemu-compose',
  files('compose.c', 'compose-dtb.c', 'compose-boot.c', 'compose-kmh.c'),
  include_directories: include_directories('..'),
  dependencies: [fdt, glib],
  install: true)
import('pkgconfig').generate(libqemu_compose,
  name: 'libqemu-compose',
  filebase: 'libqemu-compose',
  description: 'Host-side C modules for libqemu SoC composition',
  subdirs: '.',
  requires_private: ['glib-2.0'],
  libraries_private: ['-lfdt'])

libqemu_compose_test = executable('libqemu-compose-test',
  '../tests/libqemu/compose.c',
  include_directories: include_directories('..'),
  link_with: libqemu_compose,
  dependencies: [cc.find_library('dl'), fdt, glib])
libqemu_example = executable('libqemu-run', 'examples/run.c',
  include_directories: include_directories('..'),
  dependencies: [cc.find_library('dl'), threads, glib],
  install: false)

libqemu_lifecycle = executable('libqemu-lifecycle',
  '../tests/libqemu/lifecycle.c',
  include_directories: include_directories('..'),
  dependencies: cc.find_library('dl'))

libqemu_assemble = executable('libqemu-assemble', 'examples/assemble.c',
  include_directories: include_directories('..'),
  dependencies: cc.find_library('dl'))

libqemu_concurrency = executable('libqemu-concurrency',
  '../tests/libqemu/concurrency.c',
  include_directories: include_directories('..'),
  dependencies: [cc.find_library('dl'), threads])

libqemu_kunminghu = executable('libqemu-kunminghu', 'examples/kunminghu.c',
  include_directories: include_directories('..'),
  link_with: libqemu_compose,
  dependencies: [cc.find_library('dl'), threads, fdt, glib])
libqemu_dtc = find_program('dtc', required: false)
if libqemu_dtc.found()
  custom_target('libqemu-kunminghu-dtb', input: 'examples/kunminghu.dts',
    output: 'kunminghu.dtb', command: [libqemu_dtc, '-I', 'dts', '-O', 'dtb',
                                     '-o', '@OUTPUT@', '@INPUT@'],
    build_by_default: true)
endif

libqemu_rom = executable('libqemu-rom', '../tests/libqemu/rom.c',
  include_directories: include_directories('..'),
  dependencies: cc.find_library('dl'))
