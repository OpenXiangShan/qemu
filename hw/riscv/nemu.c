/*
 * QEMU RISC-V Spike Board
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This provides a RISC-V Board with the following devices:
 *
 * 0) HTIF Console and Poweroff
 * 1) CLINT (Timer and IPI)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "qemu/typedefs.h"
#include "qemu/units.h"
#include "qom/object.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/nemu.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "hw/intc/riscv_aclint.h"
#include "chardev/char.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"

#include "hw/misc/unimp.h"
#include "hw/char/xilinx_uartlite.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/sifive_plic.h"
#include <libfdt.h>
#include <unistd.h>
#include <inttypes.h>
#include <zstd.h>
#include <zlib.h>
#include "checkpoint/checkpoint.h"

enum {
    UART0_IRQ = 10,
    RTC_IRQ = 11,
    VIRTIO_IRQ = 1, /* From IRQ to (IRQ + COUNT - 1) */
    VIRTIO_COUNT = 1,
};

enum {
    NEMU_MROM,
    NEMU_PLIC,
    NEMU_CLINT,
    NEMU_UARTLITE,
    NEMU_VIRTIO,
    NEMU_GCPT,
    NEMU_DRAM,
};

/*
 * Freedom E310 G002 and G003 supports 52 interrupt sources while
 * Freedom E310 G000 supports 51 interrupt sources. We use the value
 * of G002 and G003, so it is 53 (including interrupt source 0).
 */
#define PLIC_NUM_SOURCES 53
#define PLIC_NUM_PRIORITIES 7
#define PLIC_PRIORITY_BASE 0x00
#define PLIC_PENDING_BASE 0x1000
#define PLIC_ENABLE_BASE 0x2000
#define PLIC_ENABLE_STRIDE 0x80
#define PLIC_CONTEXT_BASE 0x200000
#define PLIC_CONTEXT_STRIDE 0x1000

static const MemMapEntry nemu_memmap[] = {
    [NEMU_MROM] = { 0x1000, 0xf000 },
    [NEMU_VIRTIO] = { 0x10001000, 0x1000 },
    [NEMU_PLIC] = { 0x3c000000, 0x4000000 },
    [NEMU_CLINT] = { 0x38000000, 0x10000 },
    [NEMU_UARTLITE] = { 0x40600000, 0x1000 },
    [NEMU_GCPT] = { 0x50000000, 0x8000000 },
    [NEMU_DRAM] = { 0x80000000, 0x0 },
};

static int load_checkpoint(MachineState *machine, const char *checkpoint_path)
{
    NEMUState *s = NEMU_MACHINE(machine);
    int fd = -1;
    int64_t compressed_size;
    int64_t decompress_result;
    char *compress_file_buf = NULL;
    int64_t load_compressed_size;

    uint64_t frame_content_size;

    if (checkpoint_path) {
        fd = open(checkpoint_path, O_RDONLY | O_BINARY);
        if (fd < 0) {
            error_report("Can't open checkpoint: %s", checkpoint_path);
            return -1;
        }

        compressed_size = lseek(fd, 0, SEEK_END);
        if (compressed_size == 0) {
            error_report("Checkpoint size could not be zero");
            return -1;
        }
        lseek(fd, 0, SEEK_SET);

        compress_file_buf = g_malloc(compressed_size);

        load_compressed_size = 0;
        while (load_compressed_size < compressed_size) {
            int64_t bytes_read = read(fd, compress_file_buf + load_compressed_size, compressed_size - load_compressed_size);
            if (bytes_read < 0) {
                error_report("Failed to read checkpoint file %s", checkpoint_path);
                free(compress_file_buf);
                close(fd);
                return -1;
            }

            if (bytes_read == 0) {
                info_report("Unexpected EOF: read %ld bytes, expected %ld\n", load_compressed_size, compressed_size);
                break;
            }

            load_compressed_size += bytes_read;
            info_report("Reading checkpoint file %s: %ld/%ld bytes\n", checkpoint_path, load_compressed_size, compressed_size);
        }

        if (load_compressed_size != compressed_size) {
            close(fd);
            g_free(compress_file_buf);
            error_report("File read error, file size: %ld, read size %ld",
                         compressed_size, load_compressed_size);
            return -1;
        }

        close(fd);

        frame_content_size =
            ZSTD_getFrameContentSize(compress_file_buf, compressed_size);

        decompress_result = ZSTD_decompress(s->memory, frame_content_size,
                                            compress_file_buf, compressed_size);

        g_free(compress_file_buf);

        if (ZSTD_isError(decompress_result)) {
            error_report("Checkpoint decompress error, %s",
                         ZSTD_getErrorName(decompress_result));
            return -1;
        }

        info_report("load checkpoint %s success, frame_content_size %ld",
                    checkpoint_path, frame_content_size);
        return 1;
    } else {
        error_report("Checkpoint path is NULL");
        return -1;
    }
}

static int load_gcpt_restore(MachineState *machine,
                             const char *gcpt_restore_path)
{
    NEMUState *s = NEMU_MACHINE(machine);
    int fd = -1;
    int gcpt_restore_file_size = 0;
    int gcpt_restore_file_read_size = 0;
    if (gcpt_restore_path) {
        fd = open(gcpt_restore_path, O_RDONLY | O_BINARY);
        if (fd < 0) {
            error_report("Can't open gcpt_restore: %s", gcpt_restore_path);
            return -1;
        }
        gcpt_restore_file_size = lseek(fd, 0, SEEK_END);
        // for now gcpt_restore cannot bigger than 1M
        if (gcpt_restore_file_size == 0 ||
            gcpt_restore_file_size > 1 * 1024 * 1024) {
            close(fd);
            error_report("Gcpt size is zero or too large");
            return -1;
        }
        lseek(fd, 0, SEEK_SET);

        if (read(fd, s->memory, gcpt_restore_file_size) !=
            gcpt_restore_file_size) {
            close(fd);
            error_report("File read error, file size: %d, read size %d",
                         gcpt_restore_file_size, gcpt_restore_file_read_size);
            return -1;
        }
        close(fd);
    }
    info_report("load gcpt_restore success, load size %d, file_path %s",
                gcpt_restore_file_size, gcpt_restore_path);

    return 1;
}

static void init_limit_instructions(MachineState *machine)
{
    NEMUState *s = NEMU_MACHINE(machine);
    FILE *simpoints_file = NULL;
    FILE *weights_file = NULL;

    if (s->nemu_args.checkpoint_mode == SimpointCheckpointing) {
        assert(s->nemu_args.cpt_interval);
        info_report("Taking simpoint checkpionts with cpt interval %lu warmup "
                    "interval %lu",
                    s->nemu_args.cpt_interval,
                    s->nemu_args.warmup_interval);

        GString *simpoints_path = g_string_new(NULL);
        GString *weights_path = g_string_new(NULL);
        g_string_printf(simpoints_path, "%s/%s",
                        s->path_manager.simpoint_path->str, "simpoints0");
        g_string_printf(weights_path, "%s/%s",
                        s->path_manager.simpoint_path->str, "weights0");

        simpoints_file = fopen(simpoints_path->str, "r");
        weights_file = fopen(weights_path->str, "r");

        assert(simpoints_file);
        assert(weights_file);

        uint64_t simpoint_location, simpoint_id, weight_id;
        char weight[128];

        while (fscanf(simpoints_file, "%lu %lu\n", &simpoint_location,
                      &simpoint_id) != EOF &&
               fscanf(weights_file, "%s %lu\n", weight, &weight_id) != EOF) {
            assert(weight_id == simpoint_id);
            GString *weight_str = g_string_new(weight);

            s->simpoint_info.cpt_instructions =
                g_list_append(s->simpoint_info.cpt_instructions,
                              GINT_TO_POINTER(simpoint_location));
            s->simpoint_info.weights =
                g_list_append(s->simpoint_info.weights, weight_str);

            info_report("Simpoint %lu: @ %lu, weight: %s", simpoint_id,
                        simpoint_location, weight);
        }

        fclose(simpoints_file);
        fclose(weights_file);

    } else if (s->nemu_args.checkpoint_mode == UniformCheckpointing) {
        info_report("Taking uniform checkpionts with interval %lu",
                    s->nemu_args.cpt_interval);
        s->checkpoint_info.next_uniform_point = s->nemu_args.cpt_interval;
    } else if (s->nemu_args.checkpoint_mode == SyncUniformCheckpoint){
        s->checkpoint_info.next_uniform_point = s->nemu_args.cpt_interval;
    }
    else{
        error_report("Checkpoint mode just support SimpointCheckpoint and "
                     "UniformCheckpoint");
        exit(1);
    }
}

static gint g_compare_path(gconstpointer a, gconstpointer b)
{
    char tmp_str[512];
    int data_a;
    int data_b;
    sscanf(((GString *)a)->str, "%s %d %s", tmp_str, &data_a, tmp_str);
    sscanf(((GString *)b)->str, "%s %d %s", tmp_str, &data_b, tmp_str);
    if (data_a == data_b) {
        return 0;
    } else if (data_a < data_b) {
        return -1;
    } else {
        return 1;
    }
}

static gint g_compare_instrs(gconstpointer a, gconstpointer b)
{
    if (GPOINTER_TO_INT(a) == GPOINTER_TO_INT(b)) {
        return 0;
    } else if (GPOINTER_TO_INT(a) < GPOINTER_TO_INT(b)) {
        return -1;
    } else {
        return 1;
    }
}

static void check_path(gpointer data, gpointer user_data)
{
    info_report("%s", ((GString *)data)->str);
}

static void check_instrs(gpointer data, gpointer user_data)
{
    info_report("%d", GPOINTER_TO_INT(data));
}

static void replace_space(gpointer data, gpointer user_data)
{
    char str_before[512];
    char str_after[512];
    int instrs;
    sscanf(((GString *)data)->str, "%s %d %s", str_before, &instrs, str_after);
    g_string_printf(data, "%s%d%s", str_before, instrs, str_after);
}


static void init_path_manager(MachineState *machine)
{
    NEMUState *s = NEMU_MACHINE(machine);

    char base_output_path[1024];

    // we need to reorganize path as /output_base_dir/config_name/workload_name
    assert(s->nemu_args.workload_name);
//    s->path_manager.workload_name = g_string_new(s->workload_name);
    assert(s->nemu_args.base_dir);
//    s->path_manager.base_dir = g_string_new(s->output_base_dir);
    assert(s->nemu_args.config_name);
//    s->path_manager.config_name = g_string_new(s->config_name);

    if ((s->nemu_args.workload_name->len + s->nemu_args.base_dir->len + s->nemu_args.config_name->len) >= 1024) {
        error_report(
            "/output_base_dir/config_name/workload_name string too long");
    }

    sprintf(base_output_path, "%s/%s/%s", s->nemu_args.base_dir->str, s->nemu_args.config_name->str,
            s->nemu_args.workload_name->str);

    info_report("PathManager: Checkpoint output path %s", base_output_path);

    // prepare simpoint path for init serializer
    if (s->nemu_args.checkpoint_mode == SimpointCheckpointing) {
        assert(s->nemu_args.simpoint_path);
        s->path_manager.simpoint_path = g_string_new(s->nemu_args.simpoint_path);
        g_string_printf(s->path_manager.simpoint_path, "%s/%s",
                        s->nemu_args.simpoint_path, s->nemu_args.workload_name->str);
    }

    // Simpoint need prepare_simpoint_path
    // Uniform need prepare interval
    init_limit_instructions(machine);

    if (s->nemu_args.checkpoint_mode == SimpointCheckpointing) {
//        g_list_foreach(s->simpoint_info.cpt_instructions, prepare_output_path,
//                       base_output_path);
        GList *iterator = NULL;
        for (iterator = s->simpoint_info.cpt_instructions; iterator; iterator = iterator->next) {
            GString *checkpoint_path = g_string_new(NULL);
            gint data_position =
                g_list_index(s->simpoint_info.cpt_instructions, iterator->data);

            g_string_printf(
                checkpoint_path, "%s/%d/_ %d _%s.gz", (char *)base_output_path,
                GPOINTER_TO_INT(iterator->data),
                GPOINTER_TO_INT(iterator->data),
                ((GString *)(g_list_nth(s->simpoint_info.weights, data_position)->data))
                    ->str);
            info_report("Serializer initfinish");
            // base_path/cpt_instruction_limit/_CptInstructionLimit_weights.gz
            info_report("Checkpoint path: %s", checkpoint_path->str);

            s->path_manager.checkpoint_path_list =
                g_list_append(s->path_manager.checkpoint_path_list, checkpoint_path);
        }

        s->path_manager.checkpoint_path_list =
            g_list_sort(s->path_manager.checkpoint_path_list, g_compare_path);

        s->simpoint_info.cpt_instructions =
            g_list_sort(s->simpoint_info.cpt_instructions, g_compare_instrs);

        g_list_foreach(s->path_manager.checkpoint_path_list, replace_space,
                       NULL);

        g_list_foreach(s->path_manager.checkpoint_path_list, check_path, NULL);
        g_list_foreach(s->simpoint_info.cpt_instructions, check_instrs, NULL);
    } else if (s->nemu_args.checkpoint_mode == UniformCheckpointing || s->nemu_args.checkpoint_mode == SyncUniformCheckpoint) {
        s->path_manager.uniform_path = g_string_new(base_output_path);
        g_string_printf(s->path_manager.uniform_path, "%s/%s", base_output_path,
                        s->nemu_args.workload_name->str);
        info_report("prepare for checkpoint %s\n",
                    s->path_manager.uniform_path->str);
    } else{
        error_report("Checkpoint mode just support SimpointCheckpoint and "
                     "UniformCheckpoint");
        exit(1);
    }
}

static void simpoint_init(MachineState *machine)
{
    NEMUState *ns = NEMU_MACHINE(machine);
    // As long as it is not NoCheckpoint mode, we need to initialize the output
    // path manager
    if (ns->nemu_args.checkpoint_mode != NoCheckpoint) {
        init_path_manager(machine);
    }
}

static void nemu_load_firmware(MachineState *machine)
{
    const MemMapEntry *memmap = nemu_memmap;
    //    char *firmware_name;
    NEMUState *s = NEMU_MACHINE(machine);
    uint64_t firmware_end_addr = 0;
    uint64_t kernel_entry = 0;
    uint64_t kernel_start_addr = 0;

    /* Find firmware */
    //    firmware_name = riscv_find_firmware(machine->firmware,
    //                        riscv_default_firmware_name(&s->soc[0]));

    //    if (firmware_name) {

    if (s->nemu_args.checkpoint) {
        info_report("Load checkpoint: %s", s->nemu_args.checkpoint);
        g_assert(load_checkpoint(machine, s->nemu_args.checkpoint));
        if (s->nemu_args.gcpt_restore) {
            info_report("Load gcpt_restore: %s", s->nemu_args.gcpt_restore);
            g_assert(load_gcpt_restore(machine, s->nemu_args.gcpt_restore));
        }
        // load checkpoint donot need to load bios or kernel
        goto prepare_start;
    }

    firmware_end_addr = riscv_find_and_load_firmware(
        machine, riscv_default_firmware_name(&s->soc[0]),
        memmap[NEMU_DRAM].base, NULL);

    if (machine->firmware && firmware_end_addr) {
        info_report("Firmware load: %s, firmware end addr: %lx\n",
                    machine->firmware, firmware_end_addr);
    }

    if (machine->kernel_filename && !kernel_entry) {
        kernel_start_addr =
            riscv_calc_kernel_start_addr(&s->soc[0], firmware_end_addr);
        kernel_entry = riscv_load_kernel(machine, &s->soc[0], kernel_start_addr,
                                         true, NULL);
        info_report("%s %lx", machine->kernel_filename, kernel_entry);
    }

prepare_start:
    /* load the reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc[0], memmap[NEMU_DRAM].base,
                              memmap[NEMU_MROM].base, memmap[NEMU_MROM].size,
                              memmap[NEMU_DRAM].base, memmap[NEMU_DRAM].base);
}

static DeviceState *nemu_create_plic(const MemMapEntry *memmap, int socket,
                                     int base_hartid, int hart_count)
{
    DeviceState *ret;
    char *plic_hart_config;

    /* Per-socket PLIC hart topology configuration string */
    plic_hart_config = riscv_plic_hart_config_string(hart_count);

    /* Per-socket PLIC */
    ret = sifive_plic_create(
        memmap[NEMU_PLIC].base + socket * memmap[NEMU_PLIC].size,
        plic_hart_config, hart_count, base_hartid, PLIC_NUM_SOURCES,
        PLIC_NUM_PRIORITIES, PLIC_PRIORITY_BASE, PLIC_PENDING_BASE,
        PLIC_ENABLE_BASE, PLIC_ENABLE_STRIDE, PLIC_CONTEXT_BASE,
        PLIC_CONTEXT_STRIDE, memmap[NEMU_PLIC].size);

    g_free(plic_hart_config);

    return ret;
}

static void nemu_machine_init(MachineState *machine)
{
    const MemMapEntry *memmap = nemu_memmap;
    NEMUState *s = NEMU_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    MemoryRegion *nemu_memory = g_new(MemoryRegion, 1);
    MemoryRegion *nemu_gcpt = g_new(MemoryRegion, 1);
    DeviceState *dev;
    char *soc_name;
    int i, base_hartid, hart_count;

    for (i = 0; i < riscv_socket_count(machine); i++) {
        if (!riscv_socket_check_hartids(machine, i)) {
            error_report("discontinuous hartids in socket%d", i);
            exit(1);
        }

        base_hartid = riscv_socket_first_hartid(machine, i);
        if (base_hartid < 0) {
            error_report("can't find hartid base for socket%d", i);
            exit(1);
        }

        hart_count = riscv_socket_hart_count(machine, i);
        if (hart_count < 0) {
            error_report("can't find hart count for socket%d", i);
            exit(1);
        }

        soc_name = g_strdup_printf("soc%d", i);
        object_initialize_child(OBJECT(machine), soc_name, &s->soc[i],
                                TYPE_RISCV_HART_ARRAY);
        g_free(soc_name);
        object_property_set_str(OBJECT(&s->soc[i]), "cpu-type",
                                machine->cpu_type, &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "hartid-base", base_hartid,
                                &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "num-harts", hart_count,
                                &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->soc[i]), &error_fatal);

        /* Core Local Interruptor (timer and IPI) for each socket */
        /* Per-socket SiFive CLINT */
        riscv_aclint_swi_create(memmap[NEMU_CLINT].base +
                                    i * memmap[NEMU_CLINT].size,
                                base_hartid, hart_count, false);

        // mtime = swi
        riscv_aclint_mtimer_create(
            memmap[NEMU_CLINT].base + i * memmap[NEMU_CLINT].size +
                RISCV_ACLINT_SWI_SIZE,
            memmap[NEMU_CLINT].size, base_hartid, hart_count,
            RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
            RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, false);

        /* Per-socket interrupt controller */
        s->irqchip[i] = nemu_create_plic(memmap, i, base_hartid, hart_count);
    }

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv.nemu.mrom",
                           memmap[NEMU_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[NEMU_MROM].base,
                                mask_rom);

    // memory
    s->memory = g_malloc(machine->ram_size);
    memory_region_init_ram_ptr(nemu_memory, NULL, "riscv.nemu.ram",
                               machine->ram_size, s->memory);
    /* register system main memory (actual RAM) */
    memory_region_add_subregion(system_memory, memmap[NEMU_DRAM].base,
                                nemu_memory);

    // gcpt device
    s->gcpt_memory = g_malloc(nemu_memmap[NEMU_GCPT].size);
    memory_region_init_ram_ptr(nemu_gcpt, NULL, "riscv.nemu.gcpt",
                               nemu_memmap[NEMU_GCPT].size, s->gcpt_memory);

    memory_region_add_subregion(system_memory, memmap[NEMU_GCPT].base,
                                nemu_gcpt);
    //
    //    /* register system main memory (actual RAM) */
    //    memory_region_add_subregion(system_memory, memmap[NEMU_DRAM].base,
    //        machine->ram);

    // uartlite
    dev = qdev_new(TYPE_XILINX_UARTLITE);
    qdev_prop_set_chr(dev, "chardev", serial_hd(0));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    memory_region_add_subregion(system_memory, memmap[NEMU_UARTLITE].base,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0));

    /* VirtIO MMIO devices */
    for (i = 0; i < VIRTIO_COUNT; i++) {
        sysbus_create_simple("virtio-mmio",
            memmap[NEMU_VIRTIO].base + i * memmap[NEMU_VIRTIO].size,
            qdev_get_gpio_in(s->irqchip[0], VIRTIO_IRQ + i));
    }

    simpoint_init(machine);
    nemu_load_firmware(machine);
    multicore_checkpoint_init(machine);
    if (s->nemu_args.limit_instrucitons == 0) {
        s->nemu_args.limit_instrucitons = LONG_LONG_MAX;
    }
}

static void nemu_machine_instance_init(Object *obj) {}


static void nemu_machine_set_checkpoint_path(Object *obj, const char *value,
                                             Error **errp)
{
    NEMUState *ns = NEMU_MACHINE(obj);
    ns->nemu_args.checkpoint = g_strdup(value);
}

static void nemu_machine_set_gcpt_restore_path(Object *obj, const char *value,
                                               Error **errp)
{
    NEMUState *ns = NEMU_MACHINE(obj);
    ns->nemu_args.gcpt_restore = g_strdup(value);
}

static void nemu_machine_set_config_name(Object *obj, const char *value,
                                         Error **errp)
{
    NEMUState *ns = NEMU_MACHINE(obj);
    ns->nemu_args.config_name = g_string_new(value);
}

static void nemu_machine_set_output_base_dir(Object *obj, const char *value,
                                             Error **errp)
{
    NEMUState *ns = NEMU_MACHINE(obj);
    ns->nemu_args.base_dir = g_string_new(value);
}

static void nemu_machine_set_sync_interval(Object *obj, const char *value,
                                           Error **errp)
{
    NEMUState *ms = NEMU_MACHINE(obj);
    ms->nemu_args.sync_interval = atol(value);
}

static void nemu_machine_set_cpt_interval(Object *obj, const char *value,
                                          Error **errp)
{
    NEMUState *ms = NEMU_MACHINE(obj);
    ms->nemu_args.cpt_interval = atol(value);
}

static void nemu_machine_set_warmup_interval(Object *obj, const char *value,
                                             Error **errp)
{
    NEMUState *ms = NEMU_MACHINE(obj);
    ms->nemu_args.warmup_interval = atol(value);
}

static void nemu_machine_set_simpoint_path(Object *obj, const char *value,
                                           Error **errp)
{
    NEMUState *ms = NEMU_MACHINE(obj);
    ms->nemu_args.simpoint_path = g_strdup(value);
}

static void nemu_machine_set_workload_name(Object *obj, const char *value,
                                           Error **errp)
{
    NEMUState *ns = NEMU_MACHINE(obj);
    ns->nemu_args.workload_name = g_string_new(value);
}

static void nemu_machine_set_skip_boot(Object *obj, bool value,
                                           Error **errp)
{
    NEMUState *ns = NEMU_MACHINE(obj);
    ns->nemu_args.skip_boot = value;
}

static void nemu_machine_set_limit_instructions(Object *obj, const char* value,
                                                   Error **errp)
{
    NEMUState *ns = NEMU_MACHINE(obj);
    ns->nemu_args.limit_instrucitons = atol(value);
}

static void nemu_machine_set_checkpoint_mode(Object *obj, const char *value,
                                             Error **errp)
{
    NEMUState *ms = NEMU_MACHINE(obj);
    if (strcmp(value, "NoCheckpoint") == 0) {
        ms->nemu_args.checkpoint_mode = NoCheckpoint;
    } else if (strcmp(value, "SimpointCheckpoint") == 0) {
        ms->nemu_args.checkpoint_mode = SimpointCheckpointing;
    } else if (strcmp(value, "UniformCheckpoint") == 0) {
        ms->nemu_args.checkpoint_mode = UniformCheckpointing;
    } else if (strcmp(value, "SyncUniformCheckpoint") == 0) {
        ms->nemu_args.checkpoint_mode = SyncUniformCheckpoint;
    } else {
        ms->nemu_args.checkpoint_mode = NoCheckpoint;
        error_setg(errp, "Invalid checkpoint mode");
        error_append_hint(
            errp, "Valid values are Nocheckpoint, SimpointCheckpoint, and "
                  "UniformCheckpoint.\n");
    }
}

static void nemu_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V NEMU board";
    mc->init = nemu_machine_init;
    mc->max_cpus = NEMU_CPUS_MAX;
    mc->min_cpus = NEMU_CPUS_MIN;
    mc->default_cpu_type = TYPE_RISCV_CPU_BASE;
    mc->possible_cpu_arch_ids = riscv_numa_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = riscv_numa_cpu_index_to_props;
    mc->get_default_cpu_node_id = riscv_numa_get_default_cpu_node_id;
    mc->numa_mem_supported = true;
    mc->default_ram_size = 8 * GiB;
    /* platform instead of architectural choice */
    mc->cpu_cluster_has_numa_boundary = true;
    mc->default_ram_id = "riscv.nemu.ram";

    object_class_property_add_str(oc, "checkpoint", NULL,
                                  nemu_machine_set_checkpoint_path);
    object_class_property_add_str(oc, "gcpt-restore", NULL,
                                  nemu_machine_set_gcpt_restore_path);
    object_class_property_add_str(oc, "config-name", NULL,
                                  nemu_machine_set_config_name);
    object_class_property_add_str(oc, "output-base-dir", NULL,
                                  nemu_machine_set_output_base_dir);
    object_class_property_add_str(oc, "sync-interval", NULL,
                                  nemu_machine_set_sync_interval);
    object_class_property_add_str(oc, "cpt-interval", NULL,
                                  nemu_machine_set_cpt_interval);
    object_class_property_add_str(oc, "warmup-interval", NULL,
                                  nemu_machine_set_warmup_interval);
    object_class_property_add_str(oc, "simpoint-path", NULL,
                                  nemu_machine_set_simpoint_path);
    object_class_property_add_str(oc, "workload", NULL,
                                  nemu_machine_set_workload_name);
    object_class_property_add_str(oc, "checkpoint-mode", NULL,
                                  nemu_machine_set_checkpoint_mode);
    object_class_property_add_bool(oc, "skip-boot", NULL,
                                  nemu_machine_set_skip_boot);
    object_class_property_add_str(oc, "limit-instructions", NULL,
                                  nemu_machine_set_limit_instructions);
}

static const TypeInfo nemu_machine_typeinfo = {
    .name = MACHINE_TYPE_NAME("nemu"),
    .parent = TYPE_MACHINE,
    .class_init = nemu_machine_class_init,
    .instance_init = nemu_machine_instance_init,
    .instance_size = sizeof(NEMUState),
};

static void nemu_machine_init_register_types(void)
{
    type_register_static(&nemu_machine_typeinfo);
}

type_init(nemu_machine_init_register_types)
