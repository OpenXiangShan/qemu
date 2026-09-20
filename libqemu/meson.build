system_ss.add(files('libqemu.c'))
install_headers('libqemu.h', subdir: 'libqemu')
libqemu_example = executable('libqemu-run', 'examples/run.c',
  include_directories: include_directories('..'),
  dependencies: [cc.find_library('dl'), threads, glib],
  install: false)

libqemu_lifecycle = executable('libqemu-lifecycle',
  '../tests/libqemu/lifecycle.c',
  include_directories: include_directories('..'),
  dependencies: cc.find_library('dl'))
