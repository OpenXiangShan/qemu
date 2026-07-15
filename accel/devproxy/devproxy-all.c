/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>

#include "qemu/accel.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "qapi/visitor.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_host.h"
#include "hw/pci-host/gpex.h"
#include "system/address-spaces.h"
#include "system/devproxy-dma.h"
#include "system/cpus.h"
#include "system/devproxy-wire.h"
#include "system/runstate.h"
#include "system/system.h"
#include "accel/devproxy/devproxy.h"

typedef struct DevProxyRequest {
    uint32_t opcode;
    uint32_t cpu_index;
    uint32_t requester_id;
    uint32_t pasid;
    uint32_t flags;
    uint32_t size;
    hwaddr phys_addr;
    uint64_t data;
    uint64_t data_offset;
} DevProxyRequest;

typedef struct DevProxyResponse {
    int32_t status;
    uint32_t size;
    uint64_t data;
    uint64_t data_offset;
} DevProxyResponse;

typedef struct DevProxyVCPU {
    QemuMutex lock;
    QemuCond cond;
    bool busy;
    bool completed;
    DevProxyRequest request;
    DevProxyResponse response;
} DevProxyVCPU;

struct DevProxyProxyState {
    int listener_fd;
    int client_fd;
    bool stopping;
    bool thread_created;
    unsigned int vcpu_count;
    QemuMutex lock;
    QemuThread thread;
    DevProxyVCPU *vcpus;
};

#define DEVPROXY_DMA_MAP_MAGIC 0x6470766d617031ULL

typedef struct DevProxyDMAMap {
    uint64_t magic;
    hwaddr addr;
    hwaddr len;
    MemTxAttrs attrs;
    uint8_t data[];
} DevProxyDMAMap;

static bool devproxy_allowed = true;
static int devproxy_recv_full(int fd, void *buf, size_t len);
static bool devproxy_scalar_size_valid(uint32_t size);
static DevProxyState *devproxy_get_current(void);
static bool devproxy_dma_local_window_contains(DevProxyState *s,
                                               hwaddr addr, hwaddr len);

static bool devproxy_platform_dma_requester_registered(DevProxyState *s,
                                                       MemTxAttrs attrs)
{
    /*
     * Platform devices must explicitly register the requester ids that are
     * allowed to route plain address_space_memory DMA into the devproxy path.
     */
    return s && !attrs.unspecified &&
           test_bit(attrs.requester_id, s->platform_dma_requesters);
}

static DevProxyState *devproxy_get_dma_state(AddressSpace *as, hwaddr addr,
                                            hwaddr len, bool is_write,
                                            MemTxAttrs attrs)
{
    DevProxyState *s;
    MemoryRegion *mr;
    hwaddr xlat = 0;
    hwaddr xlen = len ? len : 1;
    Object *owner;

    if (!as || !len) {
        return NULL;
    }

    s = devproxy_get_current();
    if (s && as == &address_space_memory &&
        devproxy_platform_dma_requester_registered(s, attrs) &&
        !devproxy_dma_local_window_contains(s, addr, len)) {
        return s;
    }

    RCU_READ_LOCK_GUARD();
    mr = address_space_translate(as, addr, &xlat, &xlen, is_write, attrs);
    if (!mr) {
        return NULL;
    }

    while (mr->alias) {
        mr = mr->alias;
    }
    owner = mr->owner;
    if (!owner || !object_dynamic_cast(owner, TYPE_DEVPROXY_ACCEL)) {
        return NULL;
    }

    return DEVPROXY_STATE(owner);
}

static bool devproxy_dma_local_window_contains(DevProxyState *s,
                                               hwaddr addr, hwaddr len)
{
    if (!s || !s->dma_local_size || len == 0) {
        return false;
    }

    return addr >= s->dma_local_base &&
           len <= s->dma_local_size &&
           addr - s->dma_local_base <= s->dma_local_size - len;
}

static uint32_t devproxy_dma_chunk_limit(DevProxyState *s)
{
    if (s->dma_backend_mode == DEVPROXY_DMA_BACKEND_SHARED_BOUNCE) {
        return MIN(s->dma_shm_size, (uint64_t)UINT32_MAX);
    }

    return 8;
}

static uint32_t devproxy_dma_scalar_size(hwaddr addr, hwaddr remaining)
{
    uint32_t size = 8;

    while (size > remaining || (size > 1 && (addr & (size - 1)))) {
        size >>= 1;
    }

    return size;
}

static const char *devproxy_dma_backend_mode_str(DevProxyDMABackendMode mode)
{
    switch (mode) {
    case DEVPROXY_DMA_BACKEND_SOCKET:
        return "socket";
    case DEVPROXY_DMA_BACKEND_SHARED_BOUNCE:
        return "shared-bounce";
    default:
        return "unknown";
    }
}

static bool devproxy_dma_shm_path_is_posix_name(const char *path)
{
    return path && path[0] == '/' && path[1] && strchr(path + 1, '/') == NULL;
}

static int devproxy_dma_shm_open(const char *path, int flags, mode_t mode)
{
    if (devproxy_dma_shm_path_is_posix_name(path)) {
        return shm_open(path, flags, mode);
    }

    return open(path, flags, mode);
}

static void devproxy_dma_disconnect_locked(DevProxyState *s)
{
    if (s->dma_fd >= 0) {
        close(s->dma_fd);
        s->dma_fd = -1;
    }
}

static void devproxy_dma_shm_disconnect_locked(DevProxyState *s)
{
    if (s->dma_shm_ptr) {
        munmap(s->dma_shm_ptr, s->dma_shm_size);
        s->dma_shm_ptr = NULL;
    }
    if (s->dma_shm_fd >= 0) {
        close(s->dma_shm_fd);
        s->dma_shm_fd = -1;
    }
}

static int devproxy_dma_connect_locked(DevProxyState *s)
{
    Error *err = NULL;

    if (s->dma_fd >= 0) {
        return 0;
    }

    if (!s->dma_socket || !s->dma_socket[0]) {
        return -ENOTCONN;
    }

    s->dma_fd = unix_connect(s->dma_socket, &err);
    if (s->dma_fd < 0) {
        if (err) {
            error_reportf_err(err, "devproxy: dma connect failed: ");
        } else {
            error_report("devproxy: dma connect failed");
        }
        s->dma_fd = -1;
        return -ENOTCONN;
    }

    return 0;
}

static int devproxy_dma_shm_connect_locked(DevProxyState *s)
{
    void *ptr;
    int fd;

    if (s->dma_shm_ptr) {
        return 0;
    }
    if (!s->dma_shm_path || !s->dma_shm_path[0]) {
        return -EINVAL;
    }
    if (!s->dma_shm_size) {
        return -EINVAL;
    }

    fd = devproxy_dma_shm_open(s->dma_shm_path, O_RDWR, 0);
    if (fd < 0) {
        return -errno;
    }

    ptr = mmap(NULL, s->dma_shm_size, PROT_READ | PROT_WRITE,
               MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        int ret = -errno;

        close(fd);
        return ret;
    }

    s->dma_shm_fd = fd;
    s->dma_shm_ptr = ptr;
    return 0;
}

static int devproxy_dma_socket_xfer_locked(DevProxyState *s,
                                           const DevProxyRequest *req,
                                           DevProxyResponse *rsp)
{
    DevProxyWireRequest wire_req = {
        .version = cpu_to_le32(DEVPROXY_VERSION),
        .opcode = cpu_to_le32(req->opcode),
        .cpu_index = cpu_to_le32(req->cpu_index),
        .requester_id = cpu_to_le32(req->requester_id),
        .pasid = cpu_to_le32(req->pasid),
        .flags = cpu_to_le32(req->flags),
        .size = cpu_to_le32(req->size),
        .phys_addr = cpu_to_le64(req->phys_addr),
        .data = cpu_to_le64(req->data),
        .data_offset = cpu_to_le64(req->data_offset),
    };
    DevProxyWireResponse wire_rsp;
    int ret;

    ret = devproxy_dma_connect_locked(s);
    if (ret < 0) {
        return ret;
    }

    ret = qemu_send_full(s->dma_fd, &wire_req, sizeof(wire_req));
    if (ret != sizeof(wire_req)) {
        ret = ret < 0 ? -errno : -EIO;
        devproxy_dma_disconnect_locked(s);
        return ret;
    }

    ret = devproxy_recv_full(s->dma_fd, &wire_rsp, sizeof(wire_rsp));
    if (ret <= 0) {
        ret = ret < 0 ? ret : -EPIPE;
        devproxy_dma_disconnect_locked(s);
        return ret;
    }

    if (le32_to_cpu(wire_rsp.version) != DEVPROXY_VERSION) {
        devproxy_dma_disconnect_locked(s);
        return -EINVAL;
    }

    rsp->status = (int32_t)le32_to_cpu(wire_rsp.status);
    rsp->size = le32_to_cpu(wire_rsp.size);
    rsp->data = le64_to_cpu(wire_rsp.data);
    rsp->data_offset = le64_to_cpu(wire_rsp.data_offset);
    return 0;
}

static int devproxy_dma_socket_xfer(DevProxyState *s,
                                    const DevProxyRequest *req,
                                    DevProxyResponse *rsp)
{
    int ret;

    qemu_mutex_lock(&s->dma_lock);
    ret = devproxy_dma_socket_xfer_locked(s, req, rsp);
    qemu_mutex_unlock(&s->dma_lock);
    return ret;
}

static MemTxResult devproxy_dma_socket_rw(DevProxyState *s, hwaddr addr,
                                          MemTxAttrs attrs, void *buf,
                                          hwaddr len, bool is_write)
{
    uint8_t *ptr = buf;

    while (len > 0) {
        DevProxyRequest req = {};
        DevProxyResponse rsp = {};
        uint32_t size = devproxy_dma_scalar_size(addr, len);
        uint64_t data = 0;
        int ret;

        req.opcode = is_write ? DEVPROXY_OP_DMA_WRITE : DEVPROXY_OP_DMA_READ;
        req.cpu_index = current_cpu ? current_cpu->cpu_index : 0;
        req.requester_id = attrs.requester_id;
        req.pasid = 0;
        req.flags = 0;
        req.size = size;
        req.phys_addr = addr;
        if (is_write) {
            memcpy(&data, ptr, size);
            req.data = data;
        }

        ret = devproxy_dma_socket_xfer(s, &req, &rsp);
        if (ret < 0) {
            return MEMTX_ERROR;
        }
        if (rsp.status == -ENODEV) {
            return MEMTX_DECODE_ERROR;
        }
        if (rsp.status != 0 || rsp.size != size) {
            return MEMTX_ERROR;
        }
        if (!is_write) {
            data = rsp.data;
            memcpy(ptr, &data, size);
        }

        addr += size;
        ptr += size;
        len -= size;
    }

    return MEMTX_OK;
}

static MemTxResult devproxy_dma_shared_bounce_rw(DevProxyState *s, hwaddr addr,
                                                 MemTxAttrs attrs, void *buf,
                                                 hwaddr len, bool is_write)
{
    uint8_t *ptr = buf;
    uint32_t chunk_limit = devproxy_dma_chunk_limit(s);

    if (!chunk_limit) {
        return MEMTX_ERROR;
    }

    while (len > 0) {
        DevProxyRequest req = {};
        DevProxyResponse rsp = {};
        uint32_t chunk = MIN(len, (hwaddr)chunk_limit);
        int ret;

        qemu_mutex_lock(&s->dma_lock);

        ret = devproxy_dma_connect_locked(s);
        if (ret < 0) {
            goto error;
        }
        ret = devproxy_dma_shm_connect_locked(s);
        if (ret < 0) {
            goto error_disconnect;
        }

        if (is_write) {
            memcpy(s->dma_shm_ptr, ptr, chunk);
        }

        req.opcode = is_write ? DEVPROXY_OP_DMA_WRITE : DEVPROXY_OP_DMA_READ;
        req.cpu_index = current_cpu ? current_cpu->cpu_index : 0;
        req.requester_id = attrs.requester_id;
        req.pasid = 0;
        req.flags = 0;
        req.size = chunk;
        req.phys_addr = addr;
        req.data = 0;
        req.data_offset = 0;

        ret = devproxy_dma_socket_xfer_locked(s, &req, &rsp);
        if (ret < 0) {
            goto error_unlock;
        }
        if (rsp.data_offset > s->dma_shm_size ||
            rsp.size > s->dma_shm_size - rsp.data_offset) {
            ret = -EPROTO;
            goto error_disconnect;
        }
        if (rsp.status == 0 && !is_write && rsp.size == chunk) {
            memcpy(ptr, (uint8_t *)s->dma_shm_ptr + rsp.data_offset, chunk);
        }

        qemu_mutex_unlock(&s->dma_lock);

        if (rsp.status == -ENODEV) {
            return MEMTX_DECODE_ERROR;
        }
        if (rsp.status != 0 || rsp.size != chunk) {
            return MEMTX_ERROR;
        }

        addr += chunk;
        ptr += chunk;
        len -= chunk;
        continue;

error_disconnect:
        devproxy_dma_disconnect_locked(s);
        devproxy_dma_shm_disconnect_locked(s);
error_unlock:
        qemu_mutex_unlock(&s->dma_lock);
        return MEMTX_ERROR;
error:
        qemu_mutex_unlock(&s->dma_lock);
        return MEMTX_ERROR;
    }

    return MEMTX_OK;
}

MemTxResult devproxy_dma_memory_rw(AddressSpace *as, hwaddr addr,
                                   MemTxAttrs attrs, void *buf, hwaddr len,
                                   bool is_write)
{
    DevProxyState *s = devproxy_get_dma_state(as, addr, len, is_write, attrs);

    (void)attrs;

    if (len == 0) {
        return MEMTX_OK;
    }
    if (!s) {
        return MEMTX_ERROR;
    }

    if (devproxy_dma_local_window_contains(s, addr, len)) {
        return address_space_rw(&address_space_memory, addr, attrs, buf, len,
                                is_write);
    }

    switch (s->dma_backend_mode) {
    case DEVPROXY_DMA_BACKEND_SOCKET:
        return devproxy_dma_socket_rw(s, addr, attrs, buf, len, is_write);
    case DEVPROXY_DMA_BACKEND_SHARED_BOUNCE:
        return devproxy_dma_shared_bounce_rw(s, addr, attrs, buf, len,
                                             is_write);
    default:
        return MEMTX_ERROR;
    }
}

AddressSpace *devproxy_dma_get_address_space(void)
{
    DevProxyState *s = devproxy_get_current();

    if (s && s->dma_as_initialized) {
        return &s->dma_as;
    }

    return NULL;
}

bool devproxy_dma_register_platform_requester(uint32_t requester_id,
                                              Error **errp)
{
    DevProxyState *s = devproxy_get_current();

    if (!s) {
        error_setg(errp, "current accelerator is not devproxy");
        return false;
    }
    if (requester_id > UINT16_MAX) {
        error_setg(errp, "platform DMA requester-id 0x%x exceeds 16 bits",
                   requester_id);
        return false;
    }

    set_bit(requester_id, s->platform_dma_requesters);
    return true;
}

static int devproxy_dma_shared_bounce_xfer(DevProxyState *s,
                                           const DevProxyRequest *req,
                                           DevProxyResponse *rsp,
                                           uint64_t *data)
{
    uint64_t data_offset = 0;
    int ret;

    qemu_mutex_lock(&s->dma_lock);

    ret = devproxy_dma_connect_locked(s);
    if (ret < 0) {
        goto out;
    }
    ret = devproxy_dma_shm_connect_locked(s);
    if (ret < 0) {
        goto out_disconnect;
    }
    if (req->size > s->dma_shm_size) {
        ret = -E2BIG;
        goto out_disconnect;
    }

    if (req->opcode == DEVPROXY_OP_DMA_WRITE) {
        memcpy((uint8_t *)s->dma_shm_ptr + data_offset, data, req->size);
    }

    {
        DevProxyRequest bounce_req = *req;

        bounce_req.data = 0;
        bounce_req.data_offset = data_offset;
        ret = devproxy_dma_socket_xfer_locked(s, &bounce_req, rsp);
    }
    if (ret < 0) {
        goto out;
    }
    if (rsp->data_offset > s->dma_shm_size ||
        rsp->size > s->dma_shm_size - rsp->data_offset) {
        ret = -EPROTO;
        goto out_disconnect;
    }

    if (req->opcode == DEVPROXY_OP_DMA_READ &&
        rsp->status == 0 && rsp->size == req->size) {
        memcpy(data, (uint8_t *)s->dma_shm_ptr + rsp->data_offset, req->size);
    }

    ret = 0;
    goto out;

out_disconnect:
    devproxy_dma_disconnect_locked(s);
    devproxy_dma_shm_disconnect_locked(s);
out:
    qemu_mutex_unlock(&s->dma_lock);
    return ret;
}

static MemTxResult devproxy_dma_do_access(DevProxyState *s, hwaddr addr,
                                          MemTxAttrs attrs, uint64_t *data,
                                          unsigned size, bool is_write)
{
    DevProxyRequest req = {
        .opcode = is_write ? DEVPROXY_OP_DMA_WRITE : DEVPROXY_OP_DMA_READ,
        .cpu_index = current_cpu ? current_cpu->cpu_index : 0,
        .requester_id = attrs.requester_id,
        .pasid = 0,
        .flags = 0,
        .size = size,
        .phys_addr = addr,
        .data = is_write ? *data : 0,
    };
    DevProxyResponse rsp = {};
    int ret;

    /*
     * The bulk DMA path no longer uses the IO MemoryRegion callbacks, but
     * scalar accesses may still hit this helper. Keep the legacy 1/2/4/8-byte
     * validation here for those callers only.
     */
    if (!devproxy_scalar_size_valid(size)) {
        return MEMTX_ERROR;
    }

    if (devproxy_dma_local_window_contains(s, addr, size)) {
        uint8_t buf[8];
        MemTxResult result;

        if (is_write) {
            stn_le_p(buf, size, *data);
        }
        result = address_space_rw(&address_space_memory, addr, attrs, buf,
                                  size, is_write);
        if (result == MEMTX_OK && !is_write) {
            *data = ldn_le_p(buf, size);
        }
        return result;
    }

    switch (s->dma_backend_mode) {
    case DEVPROXY_DMA_BACKEND_SOCKET:
        ret = devproxy_dma_socket_xfer(s, &req, &rsp);
        break;
    case DEVPROXY_DMA_BACKEND_SHARED_BOUNCE:
        ret = devproxy_dma_shared_bounce_xfer(s, &req, &rsp, data);
        break;
    default:
        ret = -EINVAL;
        break;
    }
    if (ret < 0) {
        return MEMTX_ERROR;
    }

    if (rsp.status == -ENODEV) {
        return MEMTX_DECODE_ERROR;
    }
    if (rsp.status != 0 || rsp.size != size) {
        return MEMTX_ERROR;
    }

    if (!is_write && s->dma_backend_mode == DEVPROXY_DMA_BACKEND_SOCKET) {
        *data = rsp.data;
    }

    return MEMTX_OK;
}

bool devproxy_dma_address_space(AddressSpace *as, hwaddr addr, hwaddr len,
                                bool is_write, MemTxAttrs attrs)
{
    return devproxy_get_dma_state(as, addr, len, is_write, attrs) != NULL;
}

bool devproxy_dma_access_valid(AddressSpace *as, hwaddr addr, hwaddr len,
                               bool is_write, MemTxAttrs attrs)
{
    DevProxyState *s = devproxy_get_dma_state(as, addr, len, is_write, attrs);

    if (!s) {
        return false;
    }
    if (devproxy_dma_local_window_contains(s, addr, len)) {
        return address_space_access_valid(&address_space_memory, addr, len,
                                          is_write, attrs);
    }

    return len > 0;
}

void devproxy_dma_set_local_window(hwaddr base, hwaddr size)
{
    DevProxyState *s = devproxy_get_current();

    if (!s) {
        return;
    }

    s->dma_local_base = base;
    s->dma_local_size = size;
}

void *devproxy_dma_memory_map(AddressSpace *as, hwaddr addr, hwaddr *plen,
                              bool is_write, MemTxAttrs attrs)
{
    DevProxyState *s = devproxy_get_dma_state(as, addr, *plen, is_write, attrs);
    DevProxyDMAMap *map;
    hwaddr limit;
    MemTxResult ret;

    if (!s || !plen || *plen == 0) {
        if (plen) {
            *plen = 0;
        }
        return NULL;
    }

    limit = s->dma_backend_mode == DEVPROXY_DMA_BACKEND_SHARED_BOUNCE ?
        s->dma_shm_size : 64 * 1024;
    if (limit == 0) {
        *plen = 0;
        return NULL;
    }

    *plen = MIN(*plen, limit);
    map = g_malloc(sizeof(*map) + *plen);
    map->magic = DEVPROXY_DMA_MAP_MAGIC;
    map->addr = addr;
    map->len = *plen;
    map->attrs = attrs;

    if (!is_write) {
        ret = devproxy_dma_memory_rw(as, addr, attrs, map->data, *plen, false);
        if (ret != MEMTX_OK) {
            g_free(map);
            *plen = 0;
            return NULL;
        }
    }

    return map->data;
}

void devproxy_dma_memory_unmap(AddressSpace *as, void *buffer, hwaddr len,
                               bool is_write, hwaddr access_len)
{
    DevProxyDMAMap *map;
    if (!buffer) {
        return;
    }

    map = container_of(buffer, DevProxyDMAMap, data);
    g_assert(map->magic == DEVPROXY_DMA_MAP_MAGIC);

    if (is_write && access_len > 0) {
        devproxy_dma_memory_rw(as, map->addr, map->attrs,
                               map->data, MIN(access_len, map->len), true);
    }

    map->magic = ~DEVPROXY_DMA_MAP_MAGIC;
    g_free(map);
}

static MemTxResult devproxy_dma_mr_read(void *opaque, hwaddr addr,
                                        uint64_t *data, unsigned size,
                                        MemTxAttrs attrs)
{
    DevProxyState *s = opaque;

    *data = 0;
    return devproxy_dma_do_access(s, addr, attrs, data, size, false);
}

static MemTxResult devproxy_dma_mr_write(void *opaque, hwaddr addr,
                                         uint64_t data, unsigned size,
                                         MemTxAttrs attrs)
{
    DevProxyState *s = opaque;

    return devproxy_dma_do_access(s, addr, attrs, &data, size, true);
}

static const MemoryRegionOps devproxy_dma_mr_ops = {
    .read_with_attrs = devproxy_dma_mr_read,
    .write_with_attrs = devproxy_dma_mr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
};

static AddressSpace *devproxy_pci_iommu_get_address_space(PCIBus *bus,
                                                          void *opaque,
                                                          int devfn)
{
    DevProxyState *s = opaque;

    return &s->dma_as;
}

static const PCIIOMMUOps devproxy_pci_iommu_ops = {
    /*
     * This hook only redirects PCI bus-master transactions into the remote
     * DMA address space. It does not implement address translation.
     */
    .get_address_space = devproxy_pci_iommu_get_address_space,
};

static DevProxyState *devproxy_get_current(void)
{
    AccelState *accel = current_accel();

    if (!accel || !object_dynamic_cast(OBJECT(accel), TYPE_DEVPROXY_ACCEL)) {
        return NULL;
    }

    return DEVPROXY_STATE(accel);
}

static DevProxyProxyState *devproxy_get_proxy(DevProxyState *s)
{
    return s ? s->proxy : NULL;
}

static PCIBus *devproxy_get_machine_pci_bus(MachineState *machine)
{
    Object *obj;
    bool ambiguous = false;
    GPEXHost *gpex;

    obj = object_resolve_path_type("", TYPE_GPEX_HOST, &ambiguous);
    if (!obj || ambiguous) {
        return NULL;
    }

    gpex = GPEX_HOST(obj);
    if (!PCI_HOST_BRIDGE(gpex)->bus) {
        return NULL;
    }

    return PCI_HOST_BRIDGE(gpex)->bus;
}

static void devproxy_pci_refresh_bus_master(PCIDevice *pci_dev)
{
    AddressSpace *dma_as = pci_device_iommu_address_space(pci_dev);
    bool enabled = pci_dev->is_master;

    if (memory_region_is_mapped(&pci_dev->bus_master_enable_region)) {
        memory_region_del_subregion(&pci_dev->bus_master_container_region,
                                    &pci_dev->bus_master_enable_region);
    }
    object_unparent(OBJECT(&pci_dev->bus_master_enable_region));

    memory_region_init_alias(&pci_dev->bus_master_enable_region,
                             OBJECT(pci_dev), "bus master",
                             dma_as->root, 0, memory_region_size(dma_as->root));
    memory_region_set_enabled(&pci_dev->bus_master_enable_region, enabled);
    memory_region_add_subregion(&pci_dev->bus_master_container_region, 0,
                                &pci_dev->bus_master_enable_region);
}

static void devproxy_refresh_pci_device(PCIBus *bus, PCIDevice *pci_dev,
                                        void *opaque)
{
    devproxy_pci_refresh_bus_master(pci_dev);
}

static void devproxy_machine_done(Notifier *notifier, void *data)
{
    DevProxyState *s = container_of(notifier, DevProxyState, machine_done);
    MachineState *machine = current_machine;
    PCIBus *bus;

    if (!machine || !s->dma_as_initialized) {
        return;
    }

    bus = devproxy_get_machine_pci_bus(machine);
    if (!bus) {
        return;
    }

    if (!bus->iommu_ops) {
        pci_setup_iommu(bus, &devproxy_pci_iommu_ops, s);
    }

    if (bus->iommu_ops != &devproxy_pci_iommu_ops || bus->iommu_opaque != s) {
        return;
    }

    pci_for_each_device_under_bus(bus, devproxy_refresh_pci_device, NULL);
}

static DevProxyVCPU *devproxy_get_vcpu(DevProxyProxyState *proxy, CPUState *cpu)
{
    if (!proxy || !cpu) {
        return NULL;
    }

    if (cpu->cpu_index < 0 || cpu->cpu_index >= proxy->vcpu_count) {
        return NULL;
    }

    return &proxy->vcpus[cpu->cpu_index];
}

static int devproxy_recv_full(int fd, void *buf, size_t len)
{
    uint8_t *ptr = buf;

    while (len) {
        ssize_t ret = recv(fd, ptr, len, 0);

        if (ret == 0) {
            return 0;
        }
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }

        ptr += ret;
        len -= ret;
    }

    return 1;
}

static bool devproxy_scalar_size_valid(uint32_t size)
{
    return size == 1 || size == 2 || size == 4 || size == 8;
}

static void devproxy_fill_response(DevProxyResponse *rsp,
                                   int32_t status,
                                   uint32_t size,
                                   uint64_t data,
                                   uint64_t data_offset)
{
    rsp->status = status;
    rsp->size = size;
    rsp->data = data;
    rsp->data_offset = data_offset;
}

static void devproxy_encode_response(const DevProxyResponse *rsp,
                                     DevProxyWireResponse *wire)
{
    wire->version = cpu_to_le32(DEVPROXY_VERSION);
    wire->status = cpu_to_le32(rsp->status);
    wire->size = cpu_to_le32(rsp->size);
    wire->data = cpu_to_le64(rsp->data);
    wire->data_offset = cpu_to_le64(rsp->data_offset);
}

static void devproxy_close_client(DevProxyProxyState *proxy)
{
    int fd = -1;

    qemu_mutex_lock(&proxy->lock);
    if (proxy->client_fd >= 0) {
        fd = proxy->client_fd;
        proxy->client_fd = -1;
    }
    qemu_mutex_unlock(&proxy->lock);

    if (fd >= 0) {
        close(fd);
    }
}

static void devproxy_notify_all(DevProxyProxyState *proxy)
{
    unsigned int i;

    for (i = 0; i < proxy->vcpu_count; i++) {
        DevProxyVCPU *vcpu = &proxy->vcpus[i];

        qemu_mutex_lock(&vcpu->lock);
        qemu_cond_broadcast(&vcpu->cond);
        qemu_mutex_unlock(&vcpu->lock);
    }
}

static int devproxy_do_mmio(const DevProxyRequest *req, DevProxyResponse *rsp)
{
    uint8_t buf[8] = {};
    MemTxResult result;
    uint64_t data = 0;

    if (!devproxy_scalar_size_valid(req->size)) {
        devproxy_fill_response(rsp, -EINVAL, 0, 0, 0);
        return 0;
    }

    if (req->opcode == DEVPROXY_OP_MMIO_WRITE) {
        memcpy(buf, &req->data, req->size);
    }

    result = address_space_rw(&address_space_memory, req->phys_addr,
                              MEMTXATTRS_UNSPECIFIED, buf, req->size,
                              req->opcode == DEVPROXY_OP_MMIO_WRITE);

    if (req->opcode == DEVPROXY_OP_MMIO_READ) {
        memcpy(&data, buf, req->size);
    } else {
        data = req->data;
    }

    if (result == MEMTX_OK) {
        devproxy_fill_response(rsp, 0, req->size, data, 0);
    } else if (result == MEMTX_DECODE_ERROR) {
        devproxy_fill_response(rsp, -ENODEV, req->size, 0, 0);
    } else {
        devproxy_fill_response(rsp, -EIO, req->size, 0, 0);
    }

    return 0;
}

static int devproxy_submit_request(DevProxyState *s,
                                   const DevProxyRequest *req,
                                   DevProxyResponse *rsp)
{
    DevProxyProxyState *proxy = devproxy_get_proxy(s);
    CPUState *cpu;
    DevProxyVCPU *vcpu;

    if (!proxy) {
        devproxy_fill_response(rsp, -ENODEV, 0, 0, 0);
        return 0;
    }

    cpu = qemu_get_cpu(req->cpu_index);
    if (!cpu || !cpu->created) {
        devproxy_fill_response(rsp, -ENODEV, 0, 0, 0);
        return 0;
    }

    if (!cpu_can_run(cpu)) {
        devproxy_fill_response(rsp, -EBUSY, 0, 0, 0);
        return 0;
    }

    vcpu = devproxy_get_vcpu(proxy, cpu);
    if (!vcpu) {
        devproxy_fill_response(rsp, -ENODEV, 0, 0, 0);
        return 0;
    }

    qemu_mutex_lock(&vcpu->lock);
    if (vcpu->busy) {
        qemu_mutex_unlock(&vcpu->lock);
        devproxy_fill_response(rsp, -EBUSY, 0, 0, 0);
        return 0;
    }

    vcpu->request = *req;
    vcpu->busy = true;
    vcpu->completed = false;
    qemu_mutex_unlock(&vcpu->lock);

    qemu_cpu_kick(cpu);

    qemu_mutex_lock(&vcpu->lock);
    while (vcpu->busy && !vcpu->completed && !proxy->stopping) {
        qemu_cond_wait(&vcpu->cond, &vcpu->lock);
    }

    if (proxy->stopping) {
        vcpu->busy = false;
        vcpu->completed = false;
        qemu_mutex_unlock(&vcpu->lock);
        devproxy_fill_response(rsp, -EIO, 0, 0, 0);
        return 0;
    }

    *rsp = vcpu->response;
    vcpu->completed = false;
    qemu_mutex_unlock(&vcpu->lock);
    return 0;
}

static int devproxy_handle_client(DevProxyState *s, int fd)
{
    DevProxyProxyState *proxy = devproxy_get_proxy(s);
    DevProxyWireRequest wire_req;
    DevProxyWireResponse wire_rsp;
    DevProxyRequest req;
    DevProxyResponse rsp;
    int ret;

    while (!proxy->stopping) {
        ret = devproxy_recv_full(fd, &wire_req, sizeof(wire_req));
        if (ret <= 0) {
            return ret;
        }

        req.opcode = le32_to_cpu(wire_req.opcode);
        req.cpu_index = le32_to_cpu(wire_req.cpu_index);
        req.requester_id = le32_to_cpu(wire_req.requester_id);
        req.pasid = le32_to_cpu(wire_req.pasid);
        req.flags = le32_to_cpu(wire_req.flags);
        req.size = le32_to_cpu(wire_req.size);
        req.phys_addr = le64_to_cpu(wire_req.phys_addr);
        req.data = le64_to_cpu(wire_req.data);
        req.data_offset = le64_to_cpu(wire_req.data_offset);

        if (le32_to_cpu(wire_req.version) != DEVPROXY_VERSION) {
            devproxy_fill_response(&rsp, -EINVAL, 0, 0, 0);
        } else {
            switch (req.opcode) {
            case DEVPROXY_OP_PING:
                devproxy_fill_response(&rsp, 0, 0, 0, 0);
                break;
            case DEVPROXY_OP_MMIO_READ:
            case DEVPROXY_OP_MMIO_WRITE:
                devproxy_submit_request(s, &req, &rsp);
                break;
            default:
                devproxy_fill_response(&rsp, -EINVAL, 0, 0, 0);
                break;
            }
        }

        devproxy_encode_response(&rsp, &wire_rsp);
        if (qemu_send_full(fd, &wire_rsp,
                           sizeof(wire_rsp)) != sizeof(wire_rsp)) {
            return -errno;
        }
    }

    return 0;
}

static void *devproxy_listener_thread(void *opaque)
{
    DevProxyState *s = opaque;
    DevProxyProxyState *proxy = devproxy_get_proxy(s);

    while (!proxy->stopping) {
        int client_fd;

        client_fd = qemu_accept(proxy->listener_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (proxy->stopping) {
                break;
            }
            error_report("devproxy: accept failed: %s", strerror(errno));
            continue;
        }

        qemu_mutex_lock(&proxy->lock);
        proxy->client_fd = client_fd;
        qemu_mutex_unlock(&proxy->lock);

        devproxy_handle_client(s, client_fd);
        devproxy_close_client(proxy);
    }

    return NULL;
}

static void devproxy_cleanup(DevProxyState *s)
{
    DevProxyProxyState *proxy = devproxy_get_proxy(s);
    unsigned int i;


    if (!proxy) {
        return;
    }

    proxy->stopping = true;
    devproxy_notify_all(proxy);

    if (proxy->listener_fd >= 0) {
        socket_listen_cleanup(proxy->listener_fd, NULL);
        proxy->listener_fd = -1;
    }

    devproxy_close_client(proxy);

    if (proxy->thread_created) {
        CPUState *cpu;

        CPU_FOREACH(cpu) {
            qemu_cpu_kick(cpu);
        }
        qemu_thread_join(&proxy->thread);
    }

    for (i = 0; i < proxy->vcpu_count; i++) {
        qemu_cond_destroy(&proxy->vcpus[i].cond);
        qemu_mutex_destroy(&proxy->vcpus[i].lock);
    }

    qemu_mutex_destroy(&proxy->lock);
    g_free(proxy->vcpus);
    g_free(proxy);
    s->proxy = NULL;
}

int devproxy_init_machine(AccelState *as, MachineState *ms)
{
    DevProxyState *s = DEVPROXY_STATE(as);
    DevProxyProxyState *proxy;
    Error *err = NULL;
    unsigned int i;

    if (!s->socket || !s->socket[0]) {
        error_report("devproxy socket is required, use "
                     "-accel devproxy,socket=/path");
        return -EINVAL;
    }
    if (s->dma_backend_mode == DEVPROXY_DMA_BACKEND_SHARED_BOUNCE &&
        (!s->dma_shm_path || !s->dma_shm_path[0])) {
        error_report("devproxy dma-shm path is required");
        return -EINVAL;
    }

    proxy = g_new0(DevProxyProxyState, 1);
    proxy->listener_fd = -1;
    proxy->client_fd = -1;
    proxy->vcpu_count = ms->smp.max_cpus;
    qemu_mutex_init(&proxy->lock);
    proxy->vcpus = g_new0(DevProxyVCPU, proxy->vcpu_count);
    for (i = 0; i < proxy->vcpu_count; i++) {
        qemu_mutex_init(&proxy->vcpus[i].lock);
        qemu_cond_init(&proxy->vcpus[i].cond);
    }

    s->proxy = proxy;

    if (!s->dma_lock_initialized) {
        qemu_mutex_init(&s->dma_lock);
        s->dma_lock_initialized = true;
        s->dma_fd = -1;
    }

    if (!s->dma_as_initialized) {
        memory_region_init_io(&s->dma_mr, OBJECT(s), &devproxy_dma_mr_ops, s,
                              "devproxy-dma-mr", UINT64_MAX);
        address_space_init(&s->dma_as, &s->dma_mr, "devproxy-dma");
        s->dma_mr_initialized = true;
        s->dma_as_initialized = true;
    }

    if (!phase_check(PHASE_MACHINE_READY) && !s->machine_done.notify) {
        s->machine_done.notify = devproxy_machine_done;
        qemu_add_machine_init_done_notifier(&s->machine_done);
    }

    proxy->listener_fd = unix_listen(s->socket, &err);
    if (proxy->listener_fd < 0) {
        if (err) {
            error_reportf_err(err, "Failed to initialize devproxy listener: ");
        }
        devproxy_cleanup(s);
        return -EINVAL;
    }

    qemu_thread_create(&proxy->thread, "devproxy-listener",
                       devproxy_listener_thread, s, QEMU_THREAD_JOINABLE);
    proxy->thread_created = true;
    return 0;
}

int devproxy_cpu_exec(CPUState *cpu)
{
    DevProxyState *s = devproxy_get_current();
    DevProxyProxyState *proxy;
    DevProxyVCPU *vcpu;
    int ret;

    g_assert(s);
    proxy = devproxy_get_proxy(s);
    g_assert(proxy);

    vcpu = devproxy_get_vcpu(proxy, cpu);
    g_assert(vcpu);

    bql_unlock();
    cpu_exec_start(cpu);

    do {
        DevProxyRequest req;
        DevProxyResponse rsp;

        qemu_mutex_lock(&vcpu->lock);
        while (!vcpu->busy && !proxy->stopping &&
               !qatomic_read(&cpu->exit_request) &&
               !cpu->stop && !cpu->unplug) {
            qemu_cond_wait(&vcpu->cond, &vcpu->lock);
        }

        if (proxy->stopping || qatomic_read(&cpu->exit_request) ||
            cpu->stop || cpu->unplug) {
            qemu_mutex_unlock(&vcpu->lock);
            ret = EXCP_INTERRUPT;
            break;
        }

        req = vcpu->request;
        qemu_mutex_unlock(&vcpu->lock);

        switch (req.opcode) {
        case DEVPROXY_OP_MMIO_READ:
        case DEVPROXY_OP_MMIO_WRITE:
            devproxy_do_mmio(&req, &rsp);
            break;
        default:
            devproxy_fill_response(&rsp, -EINVAL, 0, 0, 0);
            break;
        }

        qemu_mutex_lock(&vcpu->lock);
        vcpu->response = rsp;
        vcpu->completed = true;
        vcpu->busy = false;
        qemu_cond_broadcast(&vcpu->cond);
        qemu_mutex_unlock(&vcpu->lock);
        ret = 0;
    } while (ret == 0);

    cpu_exec_end(cpu);
    bql_lock();

    if (ret < 0) {
        cpu_dump_state(cpu, stderr, CPU_DUMP_CODE);
        vm_stop(RUN_STATE_INTERNAL_ERROR);
    }

    qatomic_set(&cpu->exit_request, 0);
    return ret;
}

void devproxy_kick_vcpu(CPUState *cpu)
{
    DevProxyState *s = devproxy_get_current();
    DevProxyProxyState *proxy;
    DevProxyVCPU *vcpu;

    if (!s) {
        return;
    }

    proxy = devproxy_get_proxy(s);
    if (!proxy) {
        return;
    }

    vcpu = devproxy_get_vcpu(proxy, cpu);
    if (!vcpu) {
        return;
    }

    qemu_mutex_lock(&vcpu->lock);
    qemu_cond_broadcast(&vcpu->cond);
    qemu_mutex_unlock(&vcpu->lock);
}

static void devproxy_set_socket_path(Object *obj, const char *str, Error **errp)
{
    DevProxyState *s = DEVPROXY_STATE(obj);

    g_free(s->socket);
    s->socket = g_strdup(str);
}

static void devproxy_set_dma_socket_path(Object *obj, const char *str,
                                         Error **errp)
{
    DevProxyState *s = DEVPROXY_STATE(obj);

    g_free(s->dma_socket);
    s->dma_socket = g_strdup(str);
}

static char *devproxy_get_dma_mode(Object *obj, Error **errp)
{
    DevProxyState *s = DEVPROXY_STATE(obj);

    return g_strdup(devproxy_dma_backend_mode_str(s->dma_backend_mode));
}

static void devproxy_set_dma_mode(Object *obj, const char *str, Error **errp)
{
    DevProxyState *s = DEVPROXY_STATE(obj);

    if (strcmp(str, "socket") == 0) {
        s->dma_backend_mode = DEVPROXY_DMA_BACKEND_SOCKET;
    } else if (strcmp(str, "shared-bounce") == 0) {
        s->dma_backend_mode = DEVPROXY_DMA_BACKEND_SHARED_BOUNCE;
    } else {
        error_setg(errp, "invalid dma-mode '%s', expected 'socket' or "
                   "'shared-bounce'", str);
    }
}

static void devproxy_set_dma_shm_path(Object *obj, const char *str,
                                      Error **errp)
{
    DevProxyState *s = DEVPROXY_STATE(obj);

    g_free(s->dma_shm_path);
    s->dma_shm_path = g_strdup(str);
}

static void devproxy_get_dma_shm_size(Object *obj, Visitor *v,
                                      const char *name, void *opaque,
                                      Error **errp)
{
    DevProxyState *s = DEVPROXY_STATE(obj);
    uint64_t value = s->dma_shm_size;

    visit_type_uint64(v, name, &value, errp);
}

static void devproxy_set_dma_shm_size(Object *obj, Visitor *v,
                                      const char *name, void *opaque,
                                      Error **errp)
{
    DevProxyState *s = DEVPROXY_STATE(obj);
    uint64_t value;

    if (!visit_type_uint64(v, name, &value, errp)) {
        return;
    }
    if (value == 0) {
        error_setg(errp, "dma-shm-size must be non-zero");
        return;
    }

    s->dma_shm_size = value;
}

static void devproxy_accel_instance_init(Object *obj)
{
    DevProxyState *s = DEVPROXY_STATE(obj);
    const char *runtime_dir = g_get_user_runtime_dir();

    s->socket = NULL;
    s->dma_socket = g_build_filename(runtime_dir,
                                     "qemu-devproxy-dma.sock", NULL);
    s->dma_shm_path = g_build_filename(runtime_dir,
                                       "qemu-devproxy-dma-bounce", NULL);
    s->dma_shm_size = 64 * 1024;
    s->dma_backend_mode = DEVPROXY_DMA_BACKEND_SOCKET;
    s->dma_fd = -1;
    s->dma_shm_fd = -1;
    s->proxy = NULL;
    s->machine_done.notify = NULL;
    s->dma_shm_ptr = NULL;
    s->dma_local_base = 0;
    s->dma_local_size = 0;
    bitmap_zero(s->platform_dma_requesters,
                DEVPROXY_PLATFORM_DMA_REQUESTER_BITS);
    s->dma_lock_initialized = false;
    s->dma_root_mr_initialized = false;
    s->dma_mr_initialized = false;
    s->dma_as_initialized = false;
}

static void devproxy_accel_instance_finalize(Object *obj)
{
    DevProxyState *s = DEVPROXY_STATE(obj);

    if (s->machine_done.notify) {
        qemu_remove_machine_init_done_notifier(&s->machine_done);
        s->machine_done.notify = NULL;
    }
    if (s->dma_as_initialized) {
        address_space_destroy(&s->dma_as);
        object_unparent(OBJECT(&s->dma_mr));
        s->dma_as_initialized = false;
        s->dma_mr_initialized = false;
    }
    if (s->dma_lock_initialized) {
        qemu_mutex_lock(&s->dma_lock);
        devproxy_dma_disconnect_locked(s);
        devproxy_dma_shm_disconnect_locked(s);
        qemu_mutex_unlock(&s->dma_lock);
        qemu_mutex_destroy(&s->dma_lock);
        s->dma_lock_initialized = false;
    }
    devproxy_cleanup(s);
    g_free(s->socket);
    g_free(s->dma_socket);
    g_free(s->dma_shm_path);
}

static void devproxy_accel_class_init(ObjectClass *oc, const void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);

    ac->name = "DevProxy";
    ac->init_machine = devproxy_init_machine;
    ac->allowed = &devproxy_allowed;

    object_class_property_add_str(oc, "socket", NULL,
                                  devproxy_set_socket_path);
    object_class_property_set_description(oc, "socket",
        "Unix socket path used by the userspace device proxy");

    object_class_property_add_str(oc, "dma-socket", NULL,
                                  devproxy_set_dma_socket_path);
    object_class_property_set_description(oc, "dma-socket",
        "Unix socket path used by the DMA proxy channel");

    object_class_property_add_str(oc, "dma-mode",
                                  devproxy_get_dma_mode,
                                  devproxy_set_dma_mode);
    object_class_property_set_description(oc, "dma-mode",
        "DMA transport mode: 'socket' or 'shared-bounce'");

    object_class_property_add_str(oc, "dma-shm", NULL,
                                  devproxy_set_dma_shm_path);
    object_class_property_set_description(oc, "dma-shm",
        "POSIX shared memory object name or file path used by shared-bounce DMA");

    object_class_property_add(oc, "dma-shm-size", "uint64",
                              devproxy_get_dma_shm_size,
                              devproxy_set_dma_shm_size,
                              NULL, NULL);
    object_class_property_set_description(oc, "dma-shm-size",
        "Bounce-buffer size used by shared-bounce DMA");
}

static const TypeInfo devproxy_accel_type = {
    .name = TYPE_DEVPROXY_ACCEL,
    .parent = TYPE_ACCEL,
    .instance_init = devproxy_accel_instance_init,
    .instance_finalize = devproxy_accel_instance_finalize,
    .instance_size = sizeof(DevProxyState),
    .class_init = devproxy_accel_class_init,
};

static void devproxy_type_init(void)
{
    type_register_static(&devproxy_accel_type);
}

type_init(devproxy_type_init);
