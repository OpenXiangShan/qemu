# my-virtio-lib integration template

This directory contains a minimal integration example for:

- `libMyVirtio.a`: virtio protocol and MMIO transport logic
- `libMyVirtio_backend.a`: blk/net/console backend implementation

`virtio_template.c` is intentionally platform-neutral. It wires the real library
APIs together, while platform-specific memory, MMIO and IRQ functions are marked
as `TODO`.

## Integration flow

1. Fill `struct libvirtio_ops`.
   - `guest_mem_read()` and `guest_mem_write()` are the DMA path used by virtio
     queue processing.
   - `set_irq()` is called by the virtio library when the guest should receive
     an interrupt.
   - `blk_ops`, `net_ops` and `console_ops` bridge virtio protocol handling to
     `libMyVirtio_backend.a`.

2. Create the backend device with `virtio_backend_create()`.
   - blk: raw image path
   - net: slirp backend config
   - console: stdio/fd/pty/external backend config

3. Create the virtio MMIO device with `virtio_mmio_create()`.

4. Route platform MMIO accesses to:
   - `virtio_mmio_read()`
   - `virtio_mmio_write()`

5. If `virtio_mmio_write()` reports `is_doorbell`, drive the device:
   - blk: `virtio_process_req()`
   - net/console: drain readable backend packets with `virtio_receive()`

6. For backend-readable events, handle `VIRTIO_BACKEND_EVENT_READABLE` and call
   `virtio_receive()` until the guest has no available RX buffer.

## TODO hooks

The template leaves these platform hooks for the integrator:

- `template_dma_read()`
- `template_dma_write()`
- `template_set_irq()`
- platform MMIO address decoding/event loop

These are the only places that should need simulator/VMM-specific code.
