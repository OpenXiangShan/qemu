/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Stream a complete KMH checkpoint into a sparse raw RAM image. */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zstd.h>

#define BUFFER_SIZE (4U * 1024U * 1024U)

static int fail(const char *what)
{
    fprintf(stderr, "kmh-zstd-unpack: %s: %s\n", what, strerror(errno));
    return EXIT_FAILURE;
}

static bool all_zero(const unsigned char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i]) {
            return false;
        }
    }
    return true;
}

static int write_sparse(int fd, const unsigned char *p, size_t n)
{
    if (all_zero(p, n)) {
        if (lseek(fd, (off_t)n, SEEK_CUR) == (off_t)-1) {
            return -1;
        }
        return 0;
    }
    while (n) {
        ssize_t written = write(fd, p, n);
        if (written < 0) {
            return -1;
        }
        p += written;
        n -= (size_t)written;
    }
    return 0;
}

int main(int argc, char **argv)
{
    FILE *input = NULL;
    int output = -1;
    ZSTD_DCtx *dctx = NULL;
    unsigned char *in_buf = NULL, *out_buf = NULL;
    uint64_t produced = 0;
    unsigned long long expected;
    bool done = false;
    int rc = EXIT_FAILURE;

    if (argc != 3) {
        fprintf(stderr, "usage: %s INPUT.zst OUTPUT.bin\n", argv[0]);
        return EXIT_FAILURE;
    }
    input = fopen(argv[1], "rb");
    if (!input) {
        return fail(argv[1]);
    }
    output = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output < 0) {
        fail(argv[2]);
        goto out;
    }
    expected = ZSTD_getFrameContentSize(NULL, 0);
    {
        unsigned char header[64];
        size_t nr = fread(header, 1, sizeof(header), input);
        if (ferror(input) || fseek(input, 0, SEEK_SET) != 0) {
            fail("read frame header");
            goto out;
        }
        expected = ZSTD_getFrameContentSize(header, nr);
    }
    if (expected == ZSTD_CONTENTSIZE_ERROR || expected == ZSTD_CONTENTSIZE_UNKNOWN) {
        fprintf(stderr, "kmh-zstd-unpack: input must contain a known-size Zstd frame\n");
        goto out;
    }
    dctx = ZSTD_createDCtx();
    in_buf = malloc(BUFFER_SIZE);
    out_buf = malloc(BUFFER_SIZE);
    if (!dctx || !in_buf || !out_buf) {
        fprintf(stderr, "kmh-zstd-unpack: allocation failed\n");
        goto out;
    }
    while (!done) {
        size_t nr = fread(in_buf, 1, BUFFER_SIZE, input);
        ZSTD_inBuffer in = { in_buf, nr, 0 };
        if (!nr) {
            if (ferror(input)) {
                fail("read input");
            } else {
                fprintf(stderr, "kmh-zstd-unpack: truncated Zstd frame\n");
            }
            goto out;
        }
        while (in.pos < in.size) {
            ZSTD_outBuffer outbuf = { out_buf, BUFFER_SIZE, 0 };
            size_t ret = ZSTD_decompressStream(dctx, &outbuf, &in);
            if (ZSTD_isError(ret) || produced + outbuf.pos > expected) {
                fprintf(stderr, "kmh-zstd-unpack: invalid Zstd stream: %s\n",
                        ZSTD_getErrorName(ret));
                goto out;
            }
            if (write_sparse(output, out_buf, outbuf.pos) < 0) {
                fail("write output");
                goto out;
            }
            produced += outbuf.pos;
            if (!ret) {
                done = true;
                if (in.pos != in.size) {
                    fprintf(stderr, "kmh-zstd-unpack: trailing bytes after frame\n");
                    goto out;
                }
            }
        }
    }
    if (produced != expected || fgetc(input) != EOF) {
        fprintf(stderr, "kmh-zstd-unpack: output size mismatch: got %" PRIu64
                ", expected %llu\n", produced, expected);
        goto out;
    }
    if (ftruncate(output, (off_t)produced) != 0) {
        fail("truncate output");
        goto out;
    }
    if (fsync(output) != 0) {
        fail("sync output");
        goto out;
    }
    printf("decompressed=%" PRIu64 " bytes\n", produced);
    printf("output=%s\n", argv[2]);
    rc = EXIT_SUCCESS;
out:
    free(in_buf);
    free(out_buf);
    ZSTD_freeDCtx(dctx);
    if (output >= 0) {
        close(output);
    }
    if (input) {
        fclose(input);
    }
    if (rc != EXIT_SUCCESS) {
        unlink(argv[2]);
    }
    return rc;
}
