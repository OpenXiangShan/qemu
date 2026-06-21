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
```
make
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
