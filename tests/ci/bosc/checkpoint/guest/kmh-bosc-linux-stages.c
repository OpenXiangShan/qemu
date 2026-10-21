/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile uint64_t data[4096];
static const char state_file[] = "/tmp/kmh-bosc-checkpoint-state";

static void file_state(const char *value, int write_it)
{
    char buf[64] = { 0 };
    FILE *fp = fopen(state_file, write_it ? "w" : "r");

    if (!fp || (write_it ? fputs(value, fp) < 0 :
                !fgets(buf, sizeof(buf), fp)) || fclose(fp) ||
        (!write_it && strcmp(buf, value))) {
        puts("LINUX_STAGE_FAIL file state");
        exit(1);
    }
}

int main(void)
{
    uint64_t sum = 0;
    unsigned int i;

    setbuf(stdout, NULL);
    for (i = 0; i < 4096; i++) {
        data[i] = (uint64_t)i * 0x1234567 + 0xabcdef;
        sum += data[i];
    }
    file_state("stage1\n", 1);
    puts("LINUX_STAGE1_READY");
    __asm__ volatile(".word 0x0000006b" ::: "memory");

    for (i = 0; i < 4096; i++) {
        sum -= data[i];
        data[i] ^= 0x10203040;
    }
    if (sum) {
        puts("LINUX_STAGE_FAIL memory state");
        return 1;
    }
    file_state("stage1\n", 0);
    file_state("stage2\n", 1);
    puts("LINUX_STAGE2_READY");
    __asm__ volatile(".word 0x0000006b" ::: "memory");

    for (i = 0; i < 4096; i++) {
        if (data[i] != (((uint64_t)i * 0x1234567 + 0xabcdef) ^ 0x10203040)) {
            puts("LINUX_STAGE_FAIL restored memory");
            return 1;
        }
    }
    file_state("stage2\n", 0);
    puts("LINUX_STAGE3_PASS");
    return 0;
}
