# my-virtio-lib integration template

This directory contains a minimal integration example for:

- `libMyVirtio.a`: virtio protocol and MMIO transport logic
- `libMyVirtio_backend.a`: blk/net/console/gpu/input backend implementation

`virtio_template.c` is intentionally platform-neutral. It wires the real library
APIs together, while platform-specific memory, MMIO and IRQ functions are marked
as `TODO`.

## Integration flow

1. Fill `struct libvirtio_ops`.
   - `guest_mem_read()` and `guest_mem_write()` are the DMA path used by virtio
     queue processing.
   - `set_irq()` is called by the virtio library when the guest should receive
     an interrupt.
   - `blk_ops`, `net_ops`, `console_ops`, `gpu_ops` and `input_ops` bridge
     virtio protocol handling to `libMyVirtio_backend.a`.

2. Create the backend device with `virtio_backend_create()`.
   - blk: raw image path
   - net: slirp backend config
   - console: stdio/fd/pty/external backend config
   - gpu: command backend plus an optional UI/VNC backend
   - input: keyboard/mouse/tablet profiles, using either UI/VNC, evdev, or
     external event injection

3. Create the virtio MMIO device with `virtio_mmio_create_ex()`.
   Pass `struct virtio_mmio_options.packed = true` to advertise modern
   packed-ring support, or leave it false for the split-ring default.

4. Route platform MMIO accesses to:
   - `virtio_mmio_read()`
   - `virtio_mmio_write()`

5. If `virtio_mmio_write()` reports `is_doorbell`, call
   `virtio_process_req()` for every device. Then drain net/console/input
   readable backend packets/events with `virtio_receive()`.

6. For backend-readable events, handle `VIRTIO_BACKEND_EVENT_READABLE` and call
   `virtio_receive()` until the guest has no available RX buffer.

## Devices in the template

The sample `template_virtio_init()` creates these MMIO devices with split
queues. Use `template_virtio_init_ex(..., true)` to advertise packed queues:

- `0x10001000`: blk
- `0x10002000`: net
- `0x10003000`: console
- `0x10004000`: gpu, with a VNC UI backend
- `0x10005000`: keyboard, backed by the GPU UI
- `0x10006000`: mouse, backed by the GPU UI
- `0x10007000`: tablet, backed by the GPU UI

The default VNC listen address is `127.0.0.1:5915`; pass a second argv value to
the sample `main()` to override it. Pass `packed` as the third argv value to use
the packed-ring example path.

For non-VNC or additional display sinks, replace `template_gpu_scanout_update()`
and `template_gpu_scanout_disable()` with platform display code. With the
built-in VNC/UI backend, `backend_config.u.gpu.ui` is enough and these hooks can
remain no-op. This is separate from external input: for external input, create
input backends with `VIRTIO_BACKEND_INPUT_SOURCE_EXTERNAL` and inject events
through `template_input_events()`.

## TODO hooks

The template leaves these platform hooks for the integrator:

- `template_dma_read()`
- `template_dma_write()`
- `template_set_irq()`
- optional GPU scanout hooks for a non-VNC display path
- platform MMIO address decoding/event loop

These are the only places that should need simulator/VMM-specific code.
