# SPDX-License-Identifier: GPL-2.0-or-later

devproxy_ipc_inc = include_directories('include')
devproxy_ipc_lib = static_library(
  'devproxy-ipc',
  files('devproxy-ipc.c'),
  include_directories: devproxy_ipc_inc,
  dependencies: threads,
  pic: true,
  build_by_default: false,
)
devproxy_ipc = declare_dependency(
  link_with: devproxy_ipc_lib,
  include_directories: devproxy_ipc_inc,
  dependencies: threads,
)
