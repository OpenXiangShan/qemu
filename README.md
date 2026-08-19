# Overview
A lightweight, bare-metal virtio backend library that enables developers to quickly
implement virtio devices (e.g., virtio-net, virtio-blk) in hypervisors, VMMs, or
other environments.

# Features
- No OS dependencies — runs directly on physical hardware.
- Virtio 1.0 compliant - Support for split virtqueues.
- Support virtio-blk & virtio-net & virtio-console.
- Flexible I/O backend - Support for adding your own custom virtio device module.
- Isolate platform-specific code - The user implements a small set of callbacks
  for memory access, interrupt delivery...

# Usage
## Build

Build the standalone libraries with the default backend set:

```
make clean all
```

Use `BUILD_DIR` and `OUTPUT_DIR` to keep generated files outside the source
tree:

```
make BUILD_DIR=/tmp/my-virtio-build OUTPUT_DIR=/tmp/my-virtio-output clean all
```

Backend modules are enabled by default and can be omitted with Make variables.
For example, on a server that cannot build the bundled VNC backend:

```
make BACKEND_UI_VNC=n clean all
```

Available switches are `BACKEND_BLK`, `BACKEND_NET`, `BACKEND_CONSOLE`,
`BACKEND_GPU`, `BACKEND_INPUT`, and `BACKEND_UI_VNC`. Enabled values are
`1`, `y`, `yes`, or `true`; any other value disables that module.

`BACKEND_NET=n` skips `backend/virtio_backend_net.c` and the bundled
`libslirp` build. `BACKEND_UI_VNC=n` skips
`backend/virtio_backend_ui_vnc.c` and the bundled `libvncserver` build.
Disabled backend types cannot be created at runtime.

When this library is built through the QEMU subtree, pass the equivalent Meson
options from a QEMU build directory:

```
mkdir -p build-myvirtio
cd build-myvirtio
../configure --target-list=riscv64-softmmu --enable-my-virtio --disable-docs \
  -Dmy_virtio_backend_ui_vnc=false
ninja qemu-system-riscv64
```

Multiple backend modules can be disabled together:

```
make BACKEND_NET=n BACKEND_UI_VNC=n clean all
mkdir -p build-myvirtio
cd build-myvirtio
../configure --target-list=riscv64-softmmu --enable-my-virtio --disable-docs \
  -Dmy_virtio_backend_net=false -Dmy_virtio_backend_ui_vnc=false
ninja qemu-system-riscv64
```

## Source layout
```
virtio/      # Sources and internal headers for libMyVirtio.a
backend/     # Sources and internal headers for libMyVirtio_backend.a
template/    # Integration template showing how to wire both libraries
```

## Output
After building, the following files are generated under the output/ directory:
```
output/
├── libMyVirtio.a      # Static library
├── libMyVirtio_backend.a
├── virtio_wrapper.h   # Public header for libMyVirtio.a
└── virtio_backend.h   # Public header for libMyVirtio_backend.a
```

# Acknowledgements
Core virtqueue and device state machine logic derived from xvisor
(see https://github.com/xvisor/xvisor.git). Thank you to the xvisor team for
their pioneering open-source hypervisor work.

# License
GPLv2 License -- see LICENSE.
