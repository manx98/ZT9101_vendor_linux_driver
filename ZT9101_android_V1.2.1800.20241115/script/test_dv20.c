/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../os/linux/hif/dv20_protocol.h"

struct write_record { unsigned int reg, value, width; };
struct mock {
    unsigned char registers[256];
    struct write_record writes[16];
    unsigned int count, reads, ready_after, fail;
};
static unsigned int delays;

static int read8(void *context, unsigned int reg, zt_dv20_u8 *value)
{
    struct mock *m = context;
    if (m->fail)
        return -EIO;
    assert(reg < sizeof(m->registers));
    *value = m->registers[reg];
    return 0;
}

static int write8(void *context, unsigned int reg, zt_dv20_u8 value)
{
    struct mock *m = context;
    assert(m->count < 16);
    m->writes[m->count++] = (struct write_record){reg, value, 8};
    m->registers[reg] = value;
    return 0;
}

static int read32(void *context, unsigned int reg, zt_dv20_u32 *value)
{
    struct mock *m = context;
    assert(reg == 0x3e8);
    if (m->fail)
        return -EIO;
    *value = ++m->reads >= m->ready_after ? 0x55 : 0;
    return 0;
}

static int write32(void *context, unsigned int reg, zt_dv20_u32 value)
{
    struct mock *m = context;
    assert(m->count < 16);
    m->writes[m->count++] = (struct write_record){reg, value, 32};
    return 0;
}

static void delay_ms(unsigned int ms) { delays += ms; }

static void test_boot(void)
{
    struct mock m = {0};
    struct zt_dv20_io io = {&m, read8, write8, read32, write32, delay_ms};
    m.registers[0xac] = 0x10;
    m.registers[0xf8] = 0x02;
    assert(zt_dv20_usb_prepare(&io) == 0);
    assert(m.count == 5);
    assert(m.writes[0].reg == 0xac && m.writes[0].value == 0x12);
    assert(m.writes[1].reg == 0xf8 && m.writes[1].value == 0xd2);
    assert(m.writes[2].reg == 0x98 && m.writes[2].value == 0xff);
    assert(m.writes[3].reg == 0x200 && m.writes[3].value == 0x00100000);
    assert(m.writes[4].reg == 0x200 && m.writes[4].value == 0x80100000);
    m.count = 0;
    m.registers[3] = m.registers[0xf4] = 0xff;
    delays = 0;
    assert(zt_dv20_rom_prepare(&io, 1) == 0);
    assert(m.count == 3 && delays == 10);
    assert(m.writes[0].reg == 3 && m.writes[0].value == 0xfb);
    assert(m.writes[1].reg == 0xf4 && m.writes[1].value == 0xfe);
    assert(m.writes[2].reg == 3 && m.writes[2].value == 0xff);
    m.count = 0;
    assert(zt_dv20_rom_prepare(&io, 0) == 0);
    assert(m.writes[1].value == 0xff);
    m.count = 0;
    m.ready_after = 3;
    delays = 0;
    assert(zt_dv20_wait_ready(&io) == 0);
    assert(m.reads == 3 && delays == 20 && m.count == 0);
    m.reads = 0;
    m.ready_after = 51;
    delays = 0;
    assert(zt_dv20_wait_ready(&io) == -ETIMEDOUT);
    assert(m.reads == 50 && delays == 500 && m.count == 0);
    m.fail = 1;
    assert(zt_dv20_wait_ready(&io) == -EIO);
    assert(zt_dv20_usb_prepare(&io) == -EIO);
    assert(zt_dv20_rom_prepare(&io, 1) == -EIO);
    assert(m.count == 0);
}

static void put32(unsigned char *p, unsigned int value)
{
    unsigned int i;
    for (i = 0; i < 4; i++)
        p[i] = value >> (8 * i);
}

static void test_image(const char *filename)
{
    struct zt_dv20_image image;
    FILE *file = fopen(filename, "rb");
    unsigned char *data, *copy;
    long size;
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size == 59394 && fseek(file, 0, SEEK_SET) == 0);
    data = malloc((size_t)size);
    copy = malloc((size_t)size);
    assert(data && copy && fread(data, 1, (size_t)size, file) == (size_t)size);
    fclose(file);
    assert(zt_dv20_parse_image(data, (size_t)size, &image) == 0);
    assert(image.rom == 1);
    assert(image.segment[0].offset == 34 && image.segment[0].length == 38064);
    assert(image.segment[1].offset == 38130 && image.segment[1].length == 21264);
    assert(image.segment[0].version == 0x02060808);
    assert(image.segment[1].version == 0x02060802);
    assert(zt_dv20_parse_image(data, 33, &image) == -EINVAL);
    assert(zt_dv20_parse_image(data, (size_t)size - 1, &image) == -EINVAL);
    memcpy(copy, data, (size_t)size);
    copy[20] = 0; /* Duplicate M0 descriptor. */
    assert(zt_dv20_parse_image(copy, (size_t)size, &image) == -EINVAL);
    memcpy(copy, data, (size_t)size);
    put32(copy + 26, 31); /* DSP header is longer than the declared segment. */
    assert(zt_dv20_parse_image(copy, (size_t)size, &image) == -EINVAL);
    memcpy(copy, data, (size_t)size);
    put32(copy + 30, 0xffffffffu); /* Wraparound offset. */
    assert(zt_dv20_parse_image(copy, (size_t)size, &image) == -EINVAL);
    memcpy(copy, data, (size_t)size);
    put32(copy + 30, 34); /* Overlapping M0/DSP. */
    assert(zt_dv20_parse_image(copy, (size_t)size, &image) == -EINVAL);
    free(copy);
    free(data);
}

static void test_padding(void)
{
    unsigned char data[3] = {1, 2, 3}, output[8];
    assert(zt_dv20_fw_checksum(data, 3) == (0x0201 ^ 3));
    zt_dv20_fw_chunk(output, data, sizeof(data), 0, 8);
    assert(memcmp(output, data, 3) == 0);
    assert(output[3] == 0 && output[7] == 0);
    /* DV20 M0 is exactly 78 * 488 bytes: the extra final block is all padding. */
    zt_dv20_fw_chunk(output, data, sizeof(data), sizeof(data), 8);
    assert(output[0] == 0 && output[7] == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    test_boot();
    test_image(argv[1]);
    test_padding();
    puts("DV20 protocol tests passed (boot sequence, mailbox, firmware bounds, padding)");
    return 0;
}
