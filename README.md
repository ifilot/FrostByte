# FrostByte iCE FPGA Programmer

Firmware for a Raspberry Pi Pico that programs an external SPI flash used to
configure an iCE FPGA. The Pico exposes a USB CDC serial interface to the host,
holds the FPGA in reset while the flash is accessed, and speaks SPI to the flash.

The current source targets a W25Q16-style SPI flash and an iCE40UP5K-style FPGA
reset/done interface.

## Hardware Connections

Default pin assignments are in `src/constants.h`.

| Pico GPIO | Signal | Notes |
| --- | --- | --- |
| GP16 | SPI MISO | Flash DO |
| GP17 | SPI CS | Flash `/CS` |
| GP18 | SPI SCK | Flash CLK |
| GP19 | SPI MOSI | Flash DI |
| GP14 | CDONE | FPGA configuration done input |
| GP15 | CRESET | FPGA reset output, active low |
| GP25 | LED | Pico on-board LED |

The firmware starts with `CRESET` low, so the FPGA is held in reset while flash
commands are sent.

## Build

Install the Raspberry Pi Pico SDK and the ARM embedded GCC toolchain first. This
project's `src/CMakeLists.txt` currently expects the SDK at `/opt/pico-sdk` and
defaults to `PICO_BOARD=pico2` for Raspberry Pi Pico 2 / RP2350.

From the repository root:

```sh
cmake -S src -B build -j
cmake --build build -j
```

If your SDK does not have `picotool` installed and your build machine cannot
download it, disable picotool post-processing:

```sh
cmake -S src -B build -DPICO_NO_PICOTOOL=1
cmake --build build
```

To build for an original RP2040 Pico instead, configure with
`-DPICO_BOARD=pico`.

With picotool available, the normal build produces a UF2 file:

```text
build/frostbyte.uf2
```

With `PICO_NO_PICOTOOL=1`, the build still produces:

```text
build/frostbyte.elf
build/frostbyte.bin
build/frostbyte.hex
```

## Flash The Pico

1. Hold the Pico BOOTSEL button while connecting USB.
2. Copy `build/frostbyte.uf2` to the `RPI-RP2` mass-storage device.
3. Reconnect the Pico normally.
4. Open the new USB serial port.

On Linux the serial device is usually `/dev/ttyACM0`. On macOS it is usually
under `/dev/tty.usbmodem*`. On Windows it appears as a COM port.

## Program The FPGA Flash

The host-side programming script is `scripts/writefpga.py`. It talks to FrostByte
over USB CDC serial, keeps the FPGA in reset while accessing the SPI flash,
programs the ROM, verifies the written data by reading it back, then releases
the FPGA and checks CDONE.

Install the Python serial dependency if needed:

```sh
python -m pip install pyserial
```

From the repository root, program the default known-good ROM:

```sh
python scripts/writefpga.py
```

The default ROM is:

```text
rom/VERA_48.0.1.BIN
```

That file is 104,169 bytes, so the script erases two 64 KiB blocks, writes 26
4 KiB sectors, and verifies every written 256-byte page. The final partial
sector is padded with `0xFF`, matching the erased flash state.

Useful options:

```sh
python scripts/writefpga.py --port COM16
python scripts/writefpga.py --port /dev/ttyACM0
python scripts/writefpga.py path/to/other.bin
python scripts/writefpga.py --no-boot
```

A successful run should include these confirmations:

```text
Programmer ID: FROSTBYTE-v0.1.0
Flash JEDEC ID: EF 40 15
FPGA held in reset: CRESET low
Verified erased block 00
Verified erased block 01
Flash and verification completed successfully
FPGA boot confirmed: CDONE is high
```

What this proves:

- The FrostByte firmware responded over serial.
- The flash identified as the expected W25Q16-style device.
- The FPGA was held in reset while the flash was modified.
- The affected erase blocks were blank after erase.
- Every written page read back byte-for-byte against the ROM file.
- `BOOTFPGA` released `CRESET` and CDONE read high after configuration.

The script's CDONE check confirms the post-configuration value. It does not
currently sample CDONE while `CRESET` is low; add a firmware command for that if
a full low-to-high CDONE transition test is needed later.

## Serial Protocol

Commands are fixed-width 8-byte ASCII words. The firmware accepts only uppercase
letters `A` through `Z` and digits `0` through `9`; all other characters are
ignored. Every recognized or unrecognized 8-byte word is echoed back before any
command response.

Multi-byte numeric fields are ASCII hexadecimal. Binary responses are raw bytes,
not printable text.

| Command | Extra host data | Response after echo | Meaning |
| --- | --- | --- | --- |
| `READINFO` | none | 16 ASCII bytes | Board ID, currently `FROSTBYTE-v0.1.0` |
| `DEVIDSST` | none | 3 raw bytes | Flash JEDEC ID |
| `ERASBKxx` | none | 1 raw byte | Erase 64 KiB block `0xxx0000`; response is the block byte |
| `CHCKBKxx` | none | 1 raw byte | Check block erased; `0x00` means erased, `0xFF` means not erased |
| `RDPGxxxx` | none | 256 raw bytes | Read 256-byte page `0xxxxx` |
| `WRSECTxx` | 4096 raw bytes | 2 raw bytes | Write 4096-byte sector `xx`; response is CRC16/XMODEM, low byte first |
| `RESETCHP` | none | none | Send flash reset sequence |
| `HOLDFPGA` | none | 1 raw byte | Hold FPGA in reset; response is `0x00` |
| `BOOTFPGA` | none | 1 raw byte | Release FPGA reset; response is `0x01` if CDONE is high, else `0x00` |

Example: read the JEDEC ID with Python.

```python
import serial

with serial.Serial("/dev/ttyACM0", 115200, timeout=1) as port:
    port.write(b"DEVIDSST")
    echo = port.read(8)
    jedec = port.read(3)
    print(echo, jedec.hex(" "))
```

Example: write one 4096-byte sector.

```python
import serial

sector = 0x00
payload = bytes([0xFF]) * 4096

with serial.Serial("/dev/ttyACM0", 115200, timeout=5) as port:
    port.write(f"WRSECT{sector:02X}".encode("ascii"))
    echo = port.read(8)
    port.write(payload)
    crc = port.read(2)
    print(echo, crc.hex(" "))
```

## Typical Programming Flow

This is the flow implemented by `scripts/writefpga.py`:

1. Send `READINFO` and confirm the FrostByte firmware responds.
2. Send `HOLDFPGA` to hold the FPGA in reset.
3. Send `RESETCHP`.
4. Send `DEVIDSST` and confirm the expected flash responds.
5. Erase every 64 KiB block that will contain the FPGA bitstream with
   `ERASBKxx`.
6. Verify each erased block with `CHCKBKxx`.
7. Send `WRSECTxx` for each 4096-byte chunk of the bitstream and check the
   returned CRC16/XMODEM.
8. Verify by reading pages back with `RDPGxxxx`.
9. Send `BOOTFPGA` and check for a `0x01` CDONE response.

## Known Limitations

- `WRSECTxx` accepts only an 8-bit sector index. A W25Q16 is 2 MiB, which is
  512 sectors of 4096 bytes, so the current protocol can address only sectors
  `0x00` through `0xFF`.
- The firmware allocates a full 4096-byte sector buffer on the stack in
  `flash_write_sector()`. The Pico SDK default RP2040 stack is 2048 bytes, so
  writes may corrupt memory unless the buffer is moved to static storage, written
  page-by-page, or the stack size is increased.
- `flash_wait_busy()` stops waiting after 1000 ms and does not report timeout
  failure to the caller. Some 64 KiB block erases can take longer than that.
- Malformed hexadecimal fields are not rejected. For example, `ERASBKGG` is
  parsed by `strtoul()` as zero and may erase block `0x00`.
- Unknown commands are echoed but do not return an error byte, so host software
  should not wait indefinitely for a response unless it knows the command has
  one.
