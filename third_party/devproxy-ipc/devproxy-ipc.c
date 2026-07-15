/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "devproxy-ipc.h"

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define DEVPROXY_IPC_VERSION 6
#define DEVPROXY_DMA_DEFAULT_SHM_SIZE (64 * 1024U)

typedef struct DevProxyIPCWireRequest {
    uint32_t version;
    uint32_t opcode;
    uint32_t cpu_index;
    uint32_t requester_id;
    uint32_t pasid;
    uint32_t flags;
    uint32_t size;
    uint64_t phys_addr;
    uint64_t data;
    uint64_t data_offset;
} __attribute__((packed)) DevProxyIPCWireRequest;

typedef struct DevProxyIPCWireResponse {
    uint32_t version;
    int32_t status;
    uint32_t size;
    uint64_t data;
    uint64_t data_offset;
} __attribute__((packed)) DevProxyIPCWireResponse;

typedef struct DevProxyMMIOClient {
    pthread_mutex_t lock;
    char *socket_path;
    int fd;
} DevProxyMMIOClient;

typedef struct DevProxyDMAServer {
    pthread_mutex_t lock;
    char *socket_path;
    char *shm_path;
    int listener_fd;
    int client_fd;
    int shm_fd;
    bool stopping;
    bool running;
    pthread_t thread;
    DevProxyDmaTransferHandler handler;
    void *opaque;
    uint8_t *shm_ptr;
    uint32_t shm_size;
} DevProxyDMAServer;

typedef struct DevProxyIRQServer {
    pthread_mutex_t lock;
    char *socket_path;
    int listener_fd;
    int client_fd;
    bool stopping;
    bool running;
    pthread_t thread;
    DevProxyIRQSetHandler handler;
    void *opaque;
} DevProxyIRQServer;

typedef struct DevProxyMSIServer {
    pthread_mutex_t lock;
    char *socket_path;
    int listener_fd;
    int client_fd;
    bool stopping;
    bool running;
    pthread_t thread;
    DevProxyMSINotifyHandler handler;
    void *opaque;
} DevProxyMSIServer;

static int devproxy_ipc_recv_full(int fd, void *buf, size_t len)
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

static int devproxy_ipc_send_full(int fd, const void *buf, size_t len)
{
    const uint8_t *ptr = buf;

    while (len) {
        ssize_t ret = send(fd, ptr, len, 0);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }

        ptr += ret;
        len -= ret;
    }

    return 0;
}

static int devproxy_ipc_unix_connect(const char *socket_path)
{
    struct sockaddr_un addr = {};
    int fd;

    if (!socket_path || !socket_path[0]) {
        return -EINVAL;
    }

    if (strlen(socket_path) >= sizeof(addr.sun_path)) {
        return -ENAMETOOLONG;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -errno;
    }

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, socket_path);

    if (connect(fd, (struct sockaddr *)&addr, offsetof(struct sockaddr_un,
                                                        sun_path) +
                strlen(addr.sun_path) + 1) < 0) {
        int ret = -errno;

        close(fd);
        return ret;
    }

    return fd;
}

static int devproxy_ipc_unix_listen(const char *socket_path)
{
    struct sockaddr_un addr = {};
    int fd;

    if (!socket_path || !socket_path[0]) {
        return -EINVAL;
    }

    if (strlen(socket_path) >= sizeof(addr.sun_path)) {
        return -ENAMETOOLONG;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -errno;
    }

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, socket_path);
    unlink(socket_path);

    if (bind(fd, (struct sockaddr *)&addr, offsetof(struct sockaddr_un,
                                                    sun_path) +
             strlen(addr.sun_path) + 1) < 0) {
        int ret = -errno;

        close(fd);
        unlink(socket_path);
        return ret;
    }

    if (listen(fd, 1) < 0) {
        int ret = -errno;

        close(fd);
        unlink(socket_path);
        return ret;
    }

    return fd;
}

static void devproxy_ipc_shutdown_fd(int fd)
{
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
    }
}

static void devproxy_ipc_wake_listener(const char *socket_path)
{
    int fd;

    if (!socket_path || !socket_path[0]) {
        return;
    }

    fd = devproxy_ipc_unix_connect(socket_path);
    if (fd >= 0) {
        close(fd);
    }
}

static void devproxy_ipc_fill_wire_response(DevProxyIPCWireResponse *rsp,
                                            int32_t status, uint32_t size,
                                            uint64_t data,
                                            uint64_t data_offset)
{
    rsp->version = htole32(DEVPROXY_IPC_VERSION);
    rsp->status = htole32(status);
    rsp->size = htole32(size);
    rsp->data = htole64(data);
    rsp->data_offset = htole64(data_offset);
}

static int devproxy_mmio_client_connect_locked(DevProxyMMIOClient *client)
{
    int fd;

    if (client->fd >= 0) {
        return 0;
    }

    fd = devproxy_ipc_unix_connect(client->socket_path);
    if (fd < 0) {
        return fd;
    }

    client->fd = fd;
    return 0;
}

static int devproxy_mmio_client_set_socket_internal(DevProxyMMIOClient *client,
                                                    const char *socket_path)
{
    char *dup = NULL;

    if (socket_path) {
        dup = strdup(socket_path);
        if (!dup) {
            return -ENOMEM;
        }
    }

    pthread_mutex_lock(&client->lock);
    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }
    free(client->socket_path);
    client->socket_path = dup;
    pthread_mutex_unlock(&client->lock);

    return 0;
}

static void devproxy_mmio_client_disconnect_internal(DevProxyMMIOClient *client)
{
    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }
}

static int devproxy_irq_server_set_socket_internal(DevProxyIRQServer *server,
                                                   const char *socket_path)
{
    char *dup = NULL;

    if (socket_path) {
        dup = strdup(socket_path);
        if (!dup) {
            return -ENOMEM;
        }
    }

    pthread_mutex_lock(&server->lock);
    if (server->running) {
        pthread_mutex_unlock(&server->lock);
        free(dup);
        return -EBUSY;
    }
    free(server->socket_path);
    server->socket_path = dup;
    pthread_mutex_unlock(&server->lock);

    return 0;
}

static int devproxy_msi_server_set_socket_internal(DevProxyMSIServer *server,
                                                   const char *socket_path)
{
    char *dup = NULL;

    if (socket_path) {
        dup = strdup(socket_path);
        if (!dup) {
            return -ENOMEM;
        }
    }

    pthread_mutex_lock(&server->lock);
    if (server->running) {
        pthread_mutex_unlock(&server->lock);
        free(dup);
        return -EBUSY;
    }
    free(server->socket_path);
    server->socket_path = dup;
    pthread_mutex_unlock(&server->lock);

    return 0;
}

static int devproxy_mmio_do_request(DevProxyMMIOClient *client,
                                    DevProxyIPCOpcode opcode,
                                    uint32_t cpu_index, uint32_t size,
                                    uint64_t phys_addr, uint64_t data,
                                    uint64_t *read_data)
{
    DevProxyIPCWireRequest req = {
        .version = htole32(DEVPROXY_IPC_VERSION),
        .opcode = htole32(opcode),
        .cpu_index = htole32(cpu_index),
        .requester_id = htole32(0),
        .pasid = htole32(0),
        .flags = htole32(0),
        .size = htole32(size),
        .phys_addr = htole64(phys_addr),
        .data = htole64(data),
        .data_offset = htole64(0),
    };
    DevProxyIPCWireResponse wire_rsp;
    int ret;

    if (!client) {
        return -EINVAL;
    }

    pthread_mutex_lock(&client->lock);

    ret = devproxy_mmio_client_connect_locked(client);
    if (ret < 0) {
        goto out;
    }

    ret = devproxy_ipc_send_full(client->fd, &req, sizeof(req));
    if (ret < 0) {
        devproxy_mmio_client_disconnect_internal(client);
        goto out;
    }

    ret = devproxy_ipc_recv_full(client->fd, &wire_rsp, sizeof(wire_rsp));
    if (ret <= 0) {
        ret = ret < 0 ? ret : -EPIPE;
        devproxy_mmio_client_disconnect_internal(client);
        goto out;
    }

    if (le32toh(wire_rsp.version) != DEVPROXY_IPC_VERSION) {
        devproxy_mmio_client_disconnect_internal(client);
        ret = -EPROTO;
        goto out;
    }

    if (le32toh(wire_rsp.size) != size) {
        devproxy_mmio_client_disconnect_internal(client);
        ret = -EPROTO;
        goto out;
    }

    ret = (int32_t)le32toh(wire_rsp.status);
    if (ret == 0 && read_data) {
        *read_data = le64toh(wire_rsp.data);
    }

    devproxy_mmio_client_disconnect_internal(client);

out:
    pthread_mutex_unlock(&client->lock);
    return ret;
}

bool devproxy_mmio_access_size_valid(uint32_t size)
{
    return size == 1 || size == 2 || size == 4 || size == 8;
}

void *devproxy_mmio_client_create(const char *socket_path)
{
    DevProxyMMIOClient *client = calloc(1, sizeof(*client));

    if (!client) {
        return NULL;
    }

    client->fd = -1;
    if (pthread_mutex_init(&client->lock, NULL) != 0) {
        free(client);
        return NULL;
    }

    if (socket_path &&
        devproxy_mmio_client_set_socket_internal(client, socket_path) < 0) {
        devproxy_mmio_client_destroy(client);
        return NULL;
    }

    return client;
}

void devproxy_mmio_client_destroy(void *handle)
{
    DevProxyMMIOClient *client = handle;

    if (!client) {
        return;
    }

    pthread_mutex_lock(&client->lock);
    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }
    free(client->socket_path);
    client->socket_path = NULL;
    pthread_mutex_unlock(&client->lock);

    pthread_mutex_destroy(&client->lock);
    free(client);
}

int devproxy_mmio_client_read(void *handle, uint32_t cpu_index, uint32_t size,
                              uint64_t phys_addr, uint64_t *data)
{
    if (!devproxy_mmio_access_size_valid(size) || !data) {
        return -EINVAL;
    }

    return devproxy_mmio_do_request(handle, DEVPROXY_IPC_OPCODE_MMIO_READ,
                                    cpu_index, size, phys_addr, 0, data);
}

int devproxy_mmio_client_write(void *handle, uint32_t cpu_index, uint32_t size,
                               uint64_t phys_addr, uint64_t data)
{
    if (!devproxy_mmio_access_size_valid(size)) {
        return -EINVAL;
    }

    return devproxy_mmio_do_request(handle, DEVPROXY_IPC_OPCODE_MMIO_WRITE,
                                    cpu_index, size, phys_addr, data, NULL);
}

static bool devproxy_dma_length_valid(uint32_t len)
{
    return len > 0;
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

static void devproxy_dma_shm_unlink(const char *path)
{
    if (devproxy_dma_shm_path_is_posix_name(path)) {
        shm_unlink(path);
    } else if (path && path[0]) {
        unlink(path);
    }
}

static int devproxy_dma_server_setup_shm_locked(DevProxyDMAServer *server)
{
    void *ptr;
    int fd;

    if (server->shm_ptr) {
        return 0;
    }
    if (!server->shm_path || !server->shm_path[0]) {
        return -EINVAL;
    }
    if (!server->shm_size) {
        return -EINVAL;
    }

    fd = devproxy_dma_shm_open(server->shm_path, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        return -errno;
    }
    if (ftruncate(fd, server->shm_size) < 0) {
        int ret = -errno;

        close(fd);
        devproxy_dma_shm_unlink(server->shm_path);
        return ret;
    }

    ptr = mmap(NULL, server->shm_size, PROT_READ | PROT_WRITE, MAP_SHARED,
               fd, 0);
    if (ptr == MAP_FAILED) {
        int ret = -errno;

        close(fd);
        devproxy_dma_shm_unlink(server->shm_path);
        return ret;
    }

    server->shm_fd = fd;
    server->shm_ptr = ptr;
    return 0;
}

static void devproxy_dma_server_destroy_shm_locked(DevProxyDMAServer *server)
{
    if (server->shm_ptr) {
        munmap(server->shm_ptr, server->shm_size);
        server->shm_ptr = NULL;
    }
    if (server->shm_fd >= 0) {
        close(server->shm_fd);
        server->shm_fd = -1;
    }
    if (server->shm_path) {
        devproxy_dma_shm_unlink(server->shm_path);
    }
}

void *devproxy_dma_server_create(const char *socket_path,
                                 const char *shm_path,
                                 uint32_t shm_size)
{
    DevProxyDMAServer *server = calloc(1, sizeof(*server));

    if (!server) {
        return NULL;
    }

    server->listener_fd = -1;
    server->client_fd = -1;
    server->shm_fd = -1;
    server->shm_size = shm_size ? shm_size : DEVPROXY_DMA_DEFAULT_SHM_SIZE;
    if (pthread_mutex_init(&server->lock, NULL) != 0) {
        free(server);
        return NULL;
    }

    if (socket_path) {
        server->socket_path = strdup(socket_path);
        if (!server->socket_path) {
            pthread_mutex_destroy(&server->lock);
            free(server);
            return NULL;
        }
    }

    if (shm_path) {
        server->shm_path = strdup(shm_path);
        if (!server->shm_path) {
            free(server->socket_path);
            pthread_mutex_destroy(&server->lock);
            free(server);
            return NULL;
        }
    }

    return server;
}

static int devproxy_dma_server_listen_locked(DevProxyDMAServer *server)
{
    int fd;

    if (!server->socket_path || !server->socket_path[0]) {
        return -EINVAL;
    }

    if (server->listener_fd >= 0) {
        return 0;
    }

    fd = devproxy_ipc_unix_listen(server->socket_path);
    if (fd < 0) {
        return fd;
    }

    server->listener_fd = fd;
    return 0;
}

static void devproxy_dma_server_disconnect_client_locked(
    DevProxyDMAServer *server)
{
    if (server->client_fd >= 0) {
        close(server->client_fd);
        server->client_fd = -1;
    }
}

static void devproxy_dma_server_close_locked(DevProxyDMAServer *server)
{
    devproxy_dma_server_disconnect_client_locked(server);
    if (server->listener_fd >= 0) {
        close(server->listener_fd);
        server->listener_fd = -1;
    }
    if (server->socket_path) {
        unlink(server->socket_path);
    }
}

static int devproxy_dma_server_accept(DevProxyDMAServer *server)
{
    int listener_fd;
    int client_fd;

    pthread_mutex_lock(&server->lock);
    listener_fd = server->listener_fd;
    pthread_mutex_unlock(&server->lock);

    if (listener_fd < 0) {
        return -EINVAL;
    }

    client_fd = accept(listener_fd, NULL, NULL);
    if (client_fd < 0) {
        return -errno;
    }

    pthread_mutex_lock(&server->lock);
    devproxy_dma_server_disconnect_client_locked(server);
    server->client_fd = client_fd;
    pthread_mutex_unlock(&server->lock);

    return 0;
}

static int devproxy_dma_server_handle_request(DevProxyDMAServer *server)
{
    DevProxyIPCWireRequest wire_req;
    DevProxyIPCWireResponse wire_rsp;
    DevProxyDmaTransferHandler handler;
    uint8_t *shm_ptr;
    void *opaque;
    int client_fd;
    int ret;
    int status;
    uint32_t opcode;
    uint32_t cpu_index;
    uint32_t requester_id;
    uint32_t pasid;
    uint32_t flags;
    uint32_t size;
    uint64_t phys_addr;
    uint64_t data_offset;
    uint32_t response_size;
    uint64_t response_data;
    uint64_t response_data_offset;
    bool is_write;
    bool is_dma;

    if (!server) {
        return -EINVAL;
    }

    pthread_mutex_lock(&server->lock);
    client_fd = server->client_fd;
    handler = server->handler;
    opaque = server->opaque;
    shm_ptr = server->shm_ptr;
    pthread_mutex_unlock(&server->lock);

    if (client_fd < 0 || !handler || !shm_ptr) {
        return -EINVAL;
    }

    ret = devproxy_ipc_recv_full(client_fd, &wire_req, sizeof(wire_req));
    if (ret <= 0) {
        return ret < 0 ? ret : -EPIPE;
    }

    opcode = le32toh(wire_req.opcode);
    cpu_index = le32toh(wire_req.cpu_index);
    requester_id = le32toh(wire_req.requester_id);
    pasid = le32toh(wire_req.pasid);
    flags = le32toh(wire_req.flags);
    size = le32toh(wire_req.size);
    phys_addr = le64toh(wire_req.phys_addr);
    data_offset = le64toh(wire_req.data_offset);
    is_write = opcode == DEVPROXY_IPC_OPCODE_DMA_WRITE;
    is_dma = opcode == DEVPROXY_IPC_OPCODE_DMA_READ ||
             opcode == DEVPROXY_IPC_OPCODE_DMA_WRITE;
    response_size = is_dma ? size : 0;
    response_data = 0;
    response_data_offset = is_dma ? data_offset : 0;

    if (le32toh(wire_req.version) != DEVPROXY_IPC_VERSION) {
        devproxy_ipc_fill_wire_response(&wire_rsp, -EINVAL, 0, 0, 0);
    } else if (is_dma) {
        if (!devproxy_dma_length_valid(size) ||
            data_offset > server->shm_size ||
            size > server->shm_size - data_offset) {
            devproxy_ipc_fill_wire_response(&wire_rsp, -EINVAL, 0, 0, 0);
        } else {
            status = handler(opaque, opcode, cpu_index, requester_id, pasid,
                             flags, phys_addr, shm_ptr + data_offset, size,
                             is_write, &response_size, &response_data,
                             &response_data_offset);
            devproxy_ipc_fill_wire_response(&wire_rsp, status, response_size,
                                            response_data,
                                            response_data_offset);
        }
    } else {
        devproxy_ipc_fill_wire_response(&wire_rsp, -EINVAL, 0, 0, 0);
    }

    ret = devproxy_ipc_send_full(client_fd, &wire_rsp, sizeof(wire_rsp));
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static void *devproxy_dma_server_thread(void *opaque)
{
    DevProxyDMAServer *server = opaque;
    int ret;

    for (;;) {
        pthread_mutex_lock(&server->lock);
        if (server->stopping) {
            pthread_mutex_unlock(&server->lock);
            break;
        }
        pthread_mutex_unlock(&server->lock);

        ret = devproxy_dma_server_accept(server);
        if (ret < 0) {
            pthread_mutex_lock(&server->lock);
            if (server->stopping) {
                pthread_mutex_unlock(&server->lock);
                break;
            }
            pthread_mutex_unlock(&server->lock);
            continue;
        }

        for (;;) {
            pthread_mutex_lock(&server->lock);
            if (server->stopping) {
                pthread_mutex_unlock(&server->lock);
                break;
            }
            pthread_mutex_unlock(&server->lock);

            ret = devproxy_dma_server_handle_request(server);
            if (ret == 0) {
                continue;
            }
            break;
        }

        pthread_mutex_lock(&server->lock);
        devproxy_dma_server_disconnect_client_locked(server);
        pthread_mutex_unlock(&server->lock);
    }

    return NULL;
}

int devproxy_dma_server_start(void *handle, DevProxyDmaTransferHandler handler,
                              void *opaque)
{
    DevProxyDMAServer *server = handle;
    int ret;

    if (!server || !handler) {
        return -EINVAL;
    }

    pthread_mutex_lock(&server->lock);
    if (server->running) {
        pthread_mutex_unlock(&server->lock);
        return 0;
    }

    server->handler = handler;
    server->opaque = opaque;
    server->stopping = false;
    ret = devproxy_dma_server_setup_shm_locked(server);
    if (ret < 0) {
        pthread_mutex_unlock(&server->lock);
        return ret;
    }
    ret = devproxy_dma_server_listen_locked(server);
    if (ret < 0) {
        devproxy_dma_server_destroy_shm_locked(server);
        pthread_mutex_unlock(&server->lock);
        return ret;
    }
    server->running = true;
    pthread_mutex_unlock(&server->lock);

    ret = pthread_create(&server->thread, NULL, devproxy_dma_server_thread,
                         server);
    if (ret != 0) {
        pthread_mutex_lock(&server->lock);
        server->running = false;
        devproxy_dma_server_close_locked(server);
        devproxy_dma_server_destroy_shm_locked(server);
        pthread_mutex_unlock(&server->lock);
        return -ret;
    }

    return 0;
}

void devproxy_dma_server_stop(void *handle)
{
    DevProxyDMAServer *server = handle;
    bool running;
    int client_fd;
    const char *socket_path;

    if (!server) {
        return;
    }

    pthread_mutex_lock(&server->lock);
    running = server->running;
    server->stopping = true;
    client_fd = server->client_fd;
    socket_path = server->socket_path;
    pthread_mutex_unlock(&server->lock);

    devproxy_ipc_shutdown_fd(client_fd);
    if (running) {
        devproxy_ipc_wake_listener(socket_path);
    }

    if (running) {
        pthread_join(server->thread, NULL);
    }

    pthread_mutex_lock(&server->lock);
    devproxy_dma_server_close_locked(server);
    server->running = false;
    server->handler = NULL;
    server->opaque = NULL;
    devproxy_dma_server_destroy_shm_locked(server);
    pthread_mutex_unlock(&server->lock);
}

void devproxy_dma_server_destroy(void *handle)
{
    DevProxyDMAServer *server = handle;

    if (!server) {
        return;
    }

    devproxy_dma_server_stop(server);
    pthread_mutex_destroy(&server->lock);
    free(server->socket_path);
    free(server->shm_path);
    free(server);
}

static int devproxy_irq_server_listen_locked(DevProxyIRQServer *server)
{
    int fd;

    if (!server->socket_path || !server->socket_path[0]) {
        return -EINVAL;
    }

    if (server->listener_fd >= 0) {
        return 0;
    }

    fd = devproxy_ipc_unix_listen(server->socket_path);
    if (fd < 0) {
        return fd;
    }

    server->listener_fd = fd;
    return 0;
}

static void devproxy_irq_server_disconnect_client_locked(
    DevProxyIRQServer *server)
{
    if (server->client_fd >= 0) {
        close(server->client_fd);
        server->client_fd = -1;
    }
}

static void devproxy_irq_server_close_locked(DevProxyIRQServer *server)
{
    devproxy_irq_server_disconnect_client_locked(server);
    if (server->listener_fd >= 0) {
        close(server->listener_fd);
        server->listener_fd = -1;
    }
    if (server->socket_path) {
        unlink(server->socket_path);
    }
}

static int devproxy_irq_server_accept(DevProxyIRQServer *server)
{
    int listener_fd;
    int client_fd;

    pthread_mutex_lock(&server->lock);
    listener_fd = server->listener_fd;
    pthread_mutex_unlock(&server->lock);

    if (listener_fd < 0) {
        return -EINVAL;
    }

    client_fd = accept(listener_fd, NULL, NULL);
    if (client_fd < 0) {
        return -errno;
    }

    pthread_mutex_lock(&server->lock);
    devproxy_irq_server_disconnect_client_locked(server);
    server->client_fd = client_fd;
    pthread_mutex_unlock(&server->lock);

    return 0;
}

static int devproxy_irq_server_handle_request(DevProxyIRQServer *server)
{
    DevProxyIPCWireRequest wire_req;
    DevProxyIPCWireResponse wire_rsp;
    DevProxyIRQSetHandler handler;
    void *opaque;
    int client_fd;
    int ret;
    int32_t status = 0;
    uint32_t opcode;
    uint32_t size;
    uint64_t irq;
    uint64_t data;

    pthread_mutex_lock(&server->lock);
    client_fd = server->client_fd;
    handler = server->handler;
    opaque = server->opaque;
    pthread_mutex_unlock(&server->lock);

    if (client_fd < 0 || !handler) {
        return -EINVAL;
    }

    ret = devproxy_ipc_recv_full(client_fd, &wire_req, sizeof(wire_req));
    if (ret <= 0) {
        return ret < 0 ? ret : -EPIPE;
    }

    opcode = le32toh(wire_req.opcode);
    size = le32toh(wire_req.size);
    irq = le64toh(wire_req.phys_addr);
    data = le64toh(wire_req.data);

    if (le32toh(wire_req.version) != DEVPROXY_IPC_VERSION ||
        opcode != DEVPROXY_IPC_OPCODE_IRQ || size != 1 ||
        irq > UINT32_MAX || (data != 0 && data != 1)) {
        status = -EINVAL;
    } else {
        handler(opaque, (uint32_t)irq, !!data);
    }

    devproxy_ipc_fill_wire_response(&wire_rsp, status, 1, 0, 0);

    ret = devproxy_ipc_send_full(client_fd, &wire_rsp, sizeof(wire_rsp));
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static void *devproxy_irq_server_thread(void *opaque)
{
    DevProxyIRQServer *server = opaque;
    int ret;

    for (;;) {
        pthread_mutex_lock(&server->lock);
        if (server->stopping) {
            pthread_mutex_unlock(&server->lock);
            break;
        }
        pthread_mutex_unlock(&server->lock);

        ret = devproxy_irq_server_accept(server);
        if (ret < 0) {
            pthread_mutex_lock(&server->lock);
            if (server->stopping) {
                pthread_mutex_unlock(&server->lock);
                break;
            }
            pthread_mutex_unlock(&server->lock);
            continue;
        }

        for (;;) {
            pthread_mutex_lock(&server->lock);
            if (server->stopping) {
                pthread_mutex_unlock(&server->lock);
                break;
            }
            pthread_mutex_unlock(&server->lock);

            ret = devproxy_irq_server_handle_request(server);
            if (ret == 0) {
                continue;
            }
            break;
        }

        pthread_mutex_lock(&server->lock);
        devproxy_irq_server_disconnect_client_locked(server);
        pthread_mutex_unlock(&server->lock);
    }

    return NULL;
}

void *devproxy_irq_server_create(const char *socket_path)
{
    DevProxyIRQServer *server = calloc(1, sizeof(*server));

    if (!server) {
        return NULL;
    }

    server->listener_fd = -1;
    server->client_fd = -1;
    if (pthread_mutex_init(&server->lock, NULL) != 0) {
        free(server);
        return NULL;
    }

    if (socket_path &&
        devproxy_irq_server_set_socket_internal(server, socket_path) < 0) {
        devproxy_irq_server_destroy(server);
        return NULL;
    }

    return server;
}

void devproxy_irq_server_destroy(void *handle)
{
    DevProxyIRQServer *server = handle;

    if (!server) {
        return;
    }

    devproxy_irq_server_stop(server);
    pthread_mutex_destroy(&server->lock);
    free(server->socket_path);
    free(server);
}

int devproxy_irq_server_start(void *handle, DevProxyIRQSetHandler handler,
                              void *opaque)
{
    DevProxyIRQServer *server = handle;
    int ret;

    if (!server || !handler) {
        return -EINVAL;
    }

    pthread_mutex_lock(&server->lock);
    if (server->running) {
        pthread_mutex_unlock(&server->lock);
        return 0;
    }

    server->handler = handler;
    server->opaque = opaque;
    server->stopping = false;
    ret = devproxy_irq_server_listen_locked(server);
    if (ret < 0) {
        pthread_mutex_unlock(&server->lock);
        return ret;
    }
    server->running = true;
    pthread_mutex_unlock(&server->lock);

    ret = pthread_create(&server->thread, NULL, devproxy_irq_server_thread,
                         server);
    if (ret != 0) {
        pthread_mutex_lock(&server->lock);
        server->running = false;
        devproxy_irq_server_close_locked(server);
        pthread_mutex_unlock(&server->lock);
        return -ret;
    }

    return 0;
}

void devproxy_irq_server_stop(void *handle)
{
    DevProxyIRQServer *server = handle;
    bool running;
    int client_fd;
    const char *socket_path;

    if (!server) {
        return;
    }

    pthread_mutex_lock(&server->lock);
    running = server->running;
    server->stopping = true;
    client_fd = server->client_fd;
    socket_path = server->socket_path;
    pthread_mutex_unlock(&server->lock);

    devproxy_ipc_shutdown_fd(client_fd);
    if (running) {
        devproxy_ipc_wake_listener(socket_path);
    }

    if (running) {
        pthread_join(server->thread, NULL);
    }

    pthread_mutex_lock(&server->lock);
    devproxy_irq_server_close_locked(server);
    server->running = false;
    server->handler = NULL;
    server->opaque = NULL;
    pthread_mutex_unlock(&server->lock);
}

static int devproxy_msi_server_listen_locked(DevProxyMSIServer *server)
{
    int fd;

    if (!server->socket_path || !server->socket_path[0]) {
        return -EINVAL;
    }

    if (server->listener_fd >= 0) {
        return 0;
    }

    fd = devproxy_ipc_unix_listen(server->socket_path);
    if (fd < 0) {
        return fd;
    }

    server->listener_fd = fd;
    return 0;
}

static void devproxy_msi_server_disconnect_client_locked(
    DevProxyMSIServer *server)
{
    if (server->client_fd >= 0) {
        close(server->client_fd);
        server->client_fd = -1;
    }
}

static void devproxy_msi_server_close_locked(DevProxyMSIServer *server)
{
    devproxy_msi_server_disconnect_client_locked(server);
    if (server->listener_fd >= 0) {
        close(server->listener_fd);
        server->listener_fd = -1;
    }
    if (server->socket_path) {
        unlink(server->socket_path);
    }
}

static int devproxy_msi_server_accept(DevProxyMSIServer *server)
{
    int listener_fd;
    int client_fd;

    pthread_mutex_lock(&server->lock);
    listener_fd = server->listener_fd;
    pthread_mutex_unlock(&server->lock);

    if (listener_fd < 0) {
        return -EINVAL;
    }

    client_fd = accept(listener_fd, NULL, NULL);
    if (client_fd < 0) {
        return -errno;
    }

    pthread_mutex_lock(&server->lock);
    devproxy_msi_server_disconnect_client_locked(server);
    server->client_fd = client_fd;
    pthread_mutex_unlock(&server->lock);

    return 0;
}

static int devproxy_msi_server_handle_request(DevProxyMSIServer *server)
{
    DevProxyIPCWireRequest wire_req;
    DevProxyIPCWireResponse wire_rsp;
    DevProxyMSINotifyHandler handler;
    void *opaque;
    int client_fd;
    int ret;
    int32_t status = 0;
    uint32_t opcode;
    uint32_t size;
    uint64_t phys_addr;
    uint64_t data;

    pthread_mutex_lock(&server->lock);
    client_fd = server->client_fd;
    handler = server->handler;
    opaque = server->opaque;
    pthread_mutex_unlock(&server->lock);

    if (client_fd < 0 || !handler) {
        return -EINVAL;
    }

    ret = devproxy_ipc_recv_full(client_fd, &wire_req, sizeof(wire_req));
    if (ret <= 0) {
        return ret < 0 ? ret : -EPIPE;
    }

    opcode = le32toh(wire_req.opcode);
    size = le32toh(wire_req.size);
    phys_addr = le64toh(wire_req.phys_addr);
    data = le64toh(wire_req.data);

    if (le32toh(wire_req.version) != DEVPROXY_IPC_VERSION ||
        opcode != DEVPROXY_IPC_OPCODE_MSI || size != 4 ||
        data > UINT32_MAX || le64toh(wire_req.data_offset) != 0) {
        status = -EINVAL;
    } else {
        status = handler(opaque, phys_addr, (uint32_t)data, size);
    }

    devproxy_ipc_fill_wire_response(&wire_rsp, status, 4, 0, 0);

    ret = devproxy_ipc_send_full(client_fd, &wire_rsp, sizeof(wire_rsp));
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static void *devproxy_msi_server_thread(void *opaque)
{
    DevProxyMSIServer *server = opaque;
    int ret;

    for (;;) {
        pthread_mutex_lock(&server->lock);
        if (server->stopping) {
            pthread_mutex_unlock(&server->lock);
            break;
        }
        pthread_mutex_unlock(&server->lock);

        ret = devproxy_msi_server_accept(server);
        if (ret < 0) {
            pthread_mutex_lock(&server->lock);
            if (server->stopping) {
                pthread_mutex_unlock(&server->lock);
                break;
            }
            pthread_mutex_unlock(&server->lock);
            continue;
        }

        for (;;) {
            pthread_mutex_lock(&server->lock);
            if (server->stopping) {
                pthread_mutex_unlock(&server->lock);
                break;
            }
            pthread_mutex_unlock(&server->lock);

            ret = devproxy_msi_server_handle_request(server);
            if (ret == 0) {
                continue;
            }
            break;
        }

        pthread_mutex_lock(&server->lock);
        devproxy_msi_server_disconnect_client_locked(server);
        pthread_mutex_unlock(&server->lock);
    }

    return NULL;
}

void *devproxy_msi_server_create(const char *socket_path)
{
    DevProxyMSIServer *server = calloc(1, sizeof(*server));

    if (!server) {
        return NULL;
    }

    server->listener_fd = -1;
    server->client_fd = -1;
    if (pthread_mutex_init(&server->lock, NULL) != 0) {
        free(server);
        return NULL;
    }

    if (socket_path &&
        devproxy_msi_server_set_socket_internal(server, socket_path) < 0) {
        devproxy_msi_server_destroy(server);
        return NULL;
    }

    return server;
}

void devproxy_msi_server_destroy(void *handle)
{
    DevProxyMSIServer *server = handle;

    if (!server) {
        return;
    }

    devproxy_msi_server_stop(server);
    pthread_mutex_destroy(&server->lock);
    free(server->socket_path);
    free(server);
}

int devproxy_msi_server_start(void *handle, DevProxyMSINotifyHandler handler,
                              void *opaque)
{
    DevProxyMSIServer *server = handle;
    int ret;

    if (!server || !handler) {
        return -EINVAL;
    }

    pthread_mutex_lock(&server->lock);
    if (server->running) {
        pthread_mutex_unlock(&server->lock);
        return 0;
    }

    server->handler = handler;
    server->opaque = opaque;
    server->stopping = false;
    ret = devproxy_msi_server_listen_locked(server);
    if (ret < 0) {
        pthread_mutex_unlock(&server->lock);
        return ret;
    }
    server->running = true;
    pthread_mutex_unlock(&server->lock);

    ret = pthread_create(&server->thread, NULL, devproxy_msi_server_thread,
                         server);
    if (ret != 0) {
        pthread_mutex_lock(&server->lock);
        server->running = false;
        devproxy_msi_server_close_locked(server);
        pthread_mutex_unlock(&server->lock);
        return -ret;
    }

    return 0;
}

void devproxy_msi_server_stop(void *handle)
{
    DevProxyMSIServer *server = handle;
    bool running;
    int client_fd;
    const char *socket_path;

    if (!server) {
        return;
    }

    pthread_mutex_lock(&server->lock);
    running = server->running;
    server->stopping = true;
    client_fd = server->client_fd;
    socket_path = server->socket_path;
    pthread_mutex_unlock(&server->lock);

    devproxy_ipc_shutdown_fd(client_fd);
    if (running) {
        devproxy_ipc_wake_listener(socket_path);
    }

    if (running) {
        pthread_join(server->thread, NULL);
    }

    pthread_mutex_lock(&server->lock);
    devproxy_msi_server_close_locked(server);
    server->running = false;
    server->handler = NULL;
    server->opaque = NULL;
    pthread_mutex_unlock(&server->lock);
}
