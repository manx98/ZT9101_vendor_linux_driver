# DV20 device firmware provenance

`zt9101dv20.bin` was extracted from the user's supplied `ZTOPMICRO.exe`, not
generated from the Linux sources. Its original filename is `firmware.bin`.

- EXE SHA-256: `b2bc38df011abd97caa34f2f5ecc69525003d05577dae9c58fee5f15425d079f`
- Firmware SHA-256: `5b8b3f268ee1753b83a21609ce032e43f95affac171c59c548249666425dda7d`
- File size: 59,394 bytes; ROM type 1; interface tag 0x9188.
- M0: version bytes 8.8.6.2, offset 34, length 38,064.
- DSP: version bytes 2.8.6.2, original offset 38,098, length 21,296.
  The first 32 bytes of the DSP segment are skipped by the Windows loader.

The source code's GPL notice does not establish a license for this device
firmware. The binary retains its vendor provenance; no redistribution license
has been established by this work.
