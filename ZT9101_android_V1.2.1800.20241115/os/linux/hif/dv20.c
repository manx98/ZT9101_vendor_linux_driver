// SPDX-License-Identifier: GPL-2.0-or-later
/* ZT9101DV20 boot support, recovered from usbwifi_ndis6.sys 16.59.44.912. */
#include <linux/firmware.h>
#include <linux/usb.h>
#include "common.h"
#include "hif.h"
#include "fw_download.h"
#include "hw_ctrl.h"
#include "dv20_protocol.h"

static char *dv20_fw = "zt9101/zt9101dv20.bin";
module_param(dv20_fw, charp, 0444);
MODULE_PARM_DESC(dv20_fw, "DV20 firmware name under /lib/firmware");
MODULE_FIRMWARE("zt9101/zt9101dv20.bin");

static int dv20_read8(void *node, unsigned int reg, zt_dv20_u8 *value)
{
    return hif_io_read(node, 0, reg, (zt_s8 *)value, 1);
}

static int dv20_write8(void *node, unsigned int reg, zt_dv20_u8 value)
{
    return hif_io_write8(node, reg, value);
}

static int dv20_read32(void *node, unsigned int reg, zt_dv20_u32 *value)
{
    zt_s32 err = 0;
    *value = hif_io_read32(node, reg, &err);
    return err;
}

static int dv20_write32(void *node, unsigned int reg, zt_dv20_u32 value)
{
    return hif_io_write32(node, reg, value);
}

static void dv20_delay(unsigned int ms)
{
    zt_msleep(ms);
}

static struct zt_dv20_io dv20_io(hif_node_st *node)
{
    struct zt_dv20_io io = {
        .context = node, .read8 = dv20_read8, .write8 = dv20_write8,
        .read32 = dv20_read32, .write32 = dv20_write32, .delay_ms = dv20_delay,
    };
    return io;
}

int zt_dv20_usb_init(hif_node_st *node)
{
    struct zt_dv20_io io = dv20_io(node);
    int ret = zt_dv20_usb_prepare(&io);
    if (ret)
        LOG_E("DV20 USB register preparation failed: %d", ret);
    return ret;
}

int zt_dv20_fw_download(hif_node_st *node)
{
    const struct firmware *file;
    struct zt_dv20_image image;
    struct zt_dv20_io io = dv20_io(node);
    zt_u8 *segment[2] = {NULL, NULL};
    zt_u32 debug_args[2] = {8, 0x8000};
    unsigned int i, attempt, retry;
    int ret;

    if (node->hif_type != HIF_USB)
        return -ENODEV;
    ret = request_firmware_direct(&file, dv20_fw, &node->u.usb.pusb_intf->dev);
    if (ret) {
        LOG_E("DV20 requires %s (copy the supplied device firmware to /lib/firmware)", dv20_fw);
        return ret;
    }
    ret = zt_dv20_parse_image(file->data, file->size, &image);
    if (ret) {
        LOG_E("Invalid DV20 firmware container: %s", dv20_fw);
        goto release;
    }
    for (i = 0; i < 2; i++) {
        segment[i] = zt_kzalloc((size_t)image.segment[i].length + 4);
        if (!segment[i]) {
            ret = -ENOMEM;
            goto release;
        }
        zt_memcpy(segment[i], file->data + image.segment[i].offset,
                  image.segment[i].length);
        LOG_I("DV20 firmware %u: version=%08x size=%u ROM=%u", i,
              image.segment[i].version, image.segment[i].length, image.rom);
    }
    release_firmware(file);
    file = NULL;
    for (attempt = 0; attempt < 3; attempt++) {
        ret = zt_hw_mcu_disable(node);
        if (!ret)
            ret = zt_hw_mcu_enable(node);
        if (!ret)
            ret = zt_dv20_rom_prepare(&io, image.rom);
        if (ret)
            goto release;
        for (i = 0; i < 2; i++) {
            for (retry = 0; retry < 3; retry++) {
                ret = hif_write_firmware(node, i, segment[i], image.segment[i].length);
                if (!ret || node->dev_removed)
                    break;
            }
            if (ret)
                goto release;
        }
        ret = zt_hw_mcu_startup(node);
        if (ret)
            goto release;
        ret = zt_dv20_wait_ready(&io);
        if (ret == -ETIMEDOUT)
            continue;
        if (ret)
            goto release;
        for (retry = 0; retry < 4; retry++) {
            ret = hif_write_cmd(node, UMSG_OPS_HAL_DBGLOG_CONFIG, debug_args, 2, NULL, 0);
            if (!ret)
                break;
        }
        break;
    }
    if (ret)
        LOG_E("DV20 firmware startup failed: %d", ret);
    else
        LOG_I("ZT9101DV20 firmware running");
release:
    if (file)
        release_firmware(file);
    zt_kfree(segment[0]);
    zt_kfree(segment[1]);
    return ret;
}
