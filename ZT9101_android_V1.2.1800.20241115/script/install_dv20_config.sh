#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# DESTDIR permits packaging/verification without changing the running system.
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
target_root=${DESTDIR:-}
firmware_dir="$target_root/lib/firmware/zt9101"
config_dir="$target_root/etc/zt9101"
module_config="$target_root/etc/modprobe.d/zt9101.conf"
switch_config="$target_root/etc/usb_modeswitch.d/2310:1a2b"
switch_rule="$target_root/etc/udev/rules.d/40-zt9101-modeswitch.rules"

for file in zt9101dv20.bin ZT9101_fw_c1ab8ba.bin ZT9101V30_fw_8546b3a.bin; do
    test -s "$source_dir/fw/$file"
    if test -e "$firmware_dir/$file"; then
        cmp -s "$source_dir/fw/$file" "$firmware_dir/$file" || {
            echo "Existing firmware differs: $firmware_dir/$file" >&2
            exit 1
        }
    fi
done
for file in "$module_config" "$switch_config" "$switch_rule" "$config_dir/wifi.cfg"; do
    if test -L "$file"; then
        echo "Refusing symlinked configuration: $file" >&2
        exit 1
    fi
done
if test -e "$switch_config"; then
    cmp -s "$switch_config" "$source_dir/config/usb_modeswitch/2310:1a2b" || exit 1
fi
if test -e "$switch_rule"; then
    cmp -s "$switch_rule" "$source_dir/config/udev/40-zt9101-modeswitch.rules" || exit 1
fi
if test -e "$module_config"; then
    test "$(cat "$module_config")" = 'options zt9101_ztopmac_usb cfg=/etc/zt9101/wifi.cfg' || {
        echo "Existing modprobe options differ: $module_config" >&2
        exit 1
    }
fi
install -d "$firmware_dir" "$config_dir" "$(dirname "$module_config")" \
    "$(dirname "$switch_config")" "$(dirname "$switch_rule")"
for file in zt9101dv20.bin ZT9101_fw_c1ab8ba.bin ZT9101V30_fw_8546b3a.bin; do
    install -m 644 "$source_dir/fw/$file" "$firmware_dir/$file"
done
if ! test -e "$config_dir/wifi.cfg"; then
    sed -e 's|^fw=.*|fw=/lib/firmware/zt9101/ZT9101_fw_c1ab8ba.bin|' \
        -e 's|^fw1=.*|fw1=/lib/firmware/zt9101/ZT9101V30_fw_8546b3a.bin|' \
        "$source_dir/wifi.cfg" > "$config_dir/wifi.cfg"
    chmod 644 "$config_dir/wifi.cfg"
fi
printf '%s\n' 'options zt9101_ztopmac_usb cfg=/etc/zt9101/wifi.cfg' > "$module_config"
install -m 644 "$source_dir/config/usb_modeswitch/2310:1a2b" "$switch_config"
install -m 644 "$source_dir/config/udev/40-zt9101-modeswitch.rules" "$switch_rule"
if test -z "$target_root"; then
    if command -v udevadm >/dev/null 2>&1; then
        udevadm control --reload-rules
    fi
fi
echo "DV20 firmware and mode-switch configuration installed under ${target_root:-/}"
