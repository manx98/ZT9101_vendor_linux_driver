/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Wire format and boot sequence recovered from the supplied DV20 driver.
 * Kept independent of the USB transport for host-side regression testing.
 */
#ifndef ZT_DV20_PROTOCOL_H
#define ZT_DV20_PROTOCOL_H
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
typedef u8 zt_dv20_u8;
typedef u32 zt_dv20_u32;
#else
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
typedef uint8_t zt_dv20_u8;
typedef uint32_t zt_dv20_u32;
#endif

struct zt_dv20_segment {
    zt_dv20_u32 offset, length, version;
};

struct zt_dv20_image {
    zt_dv20_u8 rom;
    struct zt_dv20_segment segment[2];
};

static inline zt_dv20_u32 zt_dv20_le32(const zt_dv20_u8 *p)
{
    return (zt_dv20_u32)p[0] | (zt_dv20_u32)p[1] << 8 |
           (zt_dv20_u32)p[2] << 16 | (zt_dv20_u32)p[3] << 24;
}

static inline int zt_dv20_parse_image(const zt_dv20_u8 *data, size_t size,
                                     struct zt_dv20_image *image)
{
    unsigned int i, seen = 0;
    struct zt_dv20_image parsed = {0};
    size_t starts[2], ends[2];

    if (!data || !image || size < 34 || data[0] != 0xfa || data[1] != 0xaf ||
        data[2] != 0x88 || data[3] != 0x91 || data[4] > 1 || data[5] != 2)
        return -EINVAL;
    parsed.rom = data[4];
    for (i = 0; i < 2; i++) {
        const zt_dv20_u8 *h = data + 6 + i * 14;
        unsigned int type = h[0];
        zt_dv20_u32 offset = zt_dv20_le32(h + 10);
        zt_dv20_u32 length = zt_dv20_le32(h + 6);

        if (type > 1 || (seen & (1u << type)) || offset < 34 ||
            offset > size || !length || length > size - offset ||
            (type == 1 && length <= 32))
            return -EINVAL;
        starts[type] = offset;
        ends[type] = (size_t)offset + length;
        parsed.segment[type].offset = offset + (type ? 32 : 0);
        parsed.segment[type].length = length - (type ? 32 : 0);
        parsed.segment[type].version = zt_dv20_le32(h + 2);
        seen |= 1u << type;
    }
    if (seen != 3 || (starts[0] < ends[1] && starts[1] < ends[0]))
        return -EINVAL;
    *image = parsed;
    return 0;
}

/* Alignment bytes are zero, never bytes past the supplied allocation. */
static inline unsigned int zt_dv20_fw_checksum(const zt_dv20_u8 *data, size_t len)
{
    unsigned int sum = 0;
    size_t i;
    for (i = 0; i < len; i += 2)
        sum ^= data[i] | (i + 1 < len ? (unsigned int)data[i + 1] << 8 : 0);
    return sum;
}

static inline void zt_dv20_fw_chunk(zt_dv20_u8 *output, const zt_dv20_u8 *data,
                                   size_t size, size_t offset, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++)
        output[i] = offset < size && i < size - offset ? data[offset + i] : 0;
}

struct zt_dv20_io {
    void *context;
    int (*read8)(void *, unsigned int, zt_dv20_u8 *);
    int (*write8)(void *, unsigned int, zt_dv20_u8);
    int (*read32)(void *, unsigned int, zt_dv20_u32 *);
    int (*write32)(void *, unsigned int, zt_dv20_u32);
    void (*delay_ms)(unsigned int);
};

/* Re-enumeration does not imply a DV20 hardware reset.  Only reuse an
 * already-running firmware if the MCU startup bit and its ready mailbox
 * both agree. A stale USB PID alone is never sufficient evidence.
 * This probe is read-only and must precede any power/clock register writes.
 */
static inline int zt_dv20_fw_running(const struct zt_dv20_io *io)
{
    zt_dv20_u8 clk;
    zt_dv20_u32 ready;

    if (io->read8(io->context, 0x94, &clk) || !(clk & 0x01))
        return 0;
    if (io->read32(io->context, 0x3e8, &ready) || ready != 0x55)
        return 0;
    return 1;
}

static inline int zt_dv20_update8(const struct zt_dv20_io *io, unsigned int reg,
                                unsigned int clear, unsigned int set)
{
    zt_dv20_u8 value;
    int ret = io->read8(io->context, reg, &value);
    if (ret)
        return ret;
    return io->write8(io->context, reg, (value & ~clear) | set);
}

/* Windows 0x140021CFC: executed once after power-on, before bulk RX/download. */
static inline int zt_dv20_usb_prepare(const struct zt_dv20_io *io)
{
    int ret = zt_dv20_update8(io, 0xac, 0, 0x02);
    if (!ret)
        ret = zt_dv20_update8(io, 0xf8, 0, 0xd0);
    if (!ret)
        ret = io->write8(io->context, 0x98, 0xff);
    if (!ret)
        ret = io->write32(io->context, 0x200, 0x00100000);
    if (!ret)
        ret = io->write32(io->context, 0x200, 0x80100000);
    return ret;
}

/* Windows 0x140021D80: ROM bank selection while reg 0x03 bit 2 is clear. */
static inline int zt_dv20_rom_prepare(const struct zt_dv20_io *io, unsigned int rom)
{
    int ret;
    if (rom > 1)
        return -EINVAL;
    ret = zt_dv20_update8(io, 0x03, 0x04, 0);
    if (!ret)
        ret = zt_dv20_update8(io, 0xf4, rom ? 0x01 : 0, rom ? 0 : 0x01);
    if (!ret)
        ret = zt_dv20_update8(io, 0x03, 0, 0x04);
    if (!ret)
        io->delay_ms(10);
    return ret;
}

/* DV20 publishes 0x55 itself: do not overwrite this mailbox with a request. */
static inline int zt_dv20_wait_ready(const struct zt_dv20_io *io)
{
    unsigned int i;
    zt_dv20_u32 value;
    int ret;
    for (i = 0; i < 50; i++) {
        ret = io->read32(io->context, 0x3e8, &value);
        if (ret)
            return ret;
        if (value == 0x55)
            return 0;
        io->delay_ms(10);
    }
    return -ETIMEDOUT;
}
#endif
