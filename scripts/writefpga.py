"""Program and verify an iCE FPGA SPI flash through the FrostByte Pico bridge.

The Pico firmware exposes a small 8-byte command protocol over USB CDC serial.
This host script keeps the FPGA in reset while it erases, writes, and verifies
the external SPI flash, then releases reset and checks CDONE.
"""

import argparse
from pathlib import Path
import time

import serial
import serial.tools.list_ports


# USB CDC serial settings used by the Pico firmware.
BAUDRATE = 115200
DEFAULT_VID = 0x2E8A
DEFAULT_PID = 0x0009
EXPECTED_BOARD_ID = "FROSTBYTE-v0.1.0"
DEFAULT_JEDEC_ID = bytes([0xEF, 0x40, 0x15])

# Flash geometry expected by the firmware protocol and the W25Q16-style target.
BLOCK_SIZE = 0x10000
SECTOR_SIZE = 0x1000
PAGE_SIZE = 0x100
PAGES_PER_SECTOR = SECTOR_SIZE // PAGE_SIZE

# By default, program the known-good ROM in this repository.
REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ROM = REPO_ROOT / "rom" / "VERA_48.0.1.BIN"


class ProgrammerError(Exception):
    """Raised for protocol, verification, and preflight failures."""

    pass


def main():
    """Run the full programming flow from preflight checks through CDONE."""

    args = parse_args()
    rom_path = args.rom.resolve()

    if not rom_path.is_file():
        raise ProgrammerError(f"ROM file not found: {rom_path}")

    port = args.port or find_board(args.vid, args.pid)
    if not port:
        raise ProgrammerError(
            f"Could not find board with VID:PID {args.vid:04X}:{args.pid:04X}"
        )

    print(f"Using serial port: {port}")
    print(f"Using ROM file: {rom_path}")

    with serial.Serial(port, BAUDRATE, timeout=args.timeout) as ser:
        read_board_info(ser)
        hold_fpga(ser)
        check_flash_id(ser, args.expected_jedec)
        flash_file(ser, rom_path)
        if args.no_boot:
            print("Skipping FPGA boot/CDONE check (--no-boot).")
        else:
            boot_fpga(ser)


def parse_args():
    """Parse command-line options for selecting the ROM and serial target."""

    parser = argparse.ArgumentParser(
        description="Program and verify the FPGA SPI flash through FrostByte."
    )
    parser.add_argument(
        "rom",
        nargs="?",
        type=Path,
        default=DEFAULT_ROM,
        help=f"ROM/bitstream file to program. Default: {DEFAULT_ROM}",
    )
    parser.add_argument(
        "--port",
        help="Serial port to use instead of auto-detecting the Pico.",
    )
    parser.add_argument(
        "--vid",
        type=parse_hex_int,
        default=DEFAULT_VID,
        help="USB vendor ID used for auto-detect. Default: 0x2E8A",
    )
    parser.add_argument(
        "--pid",
        type=parse_hex_int,
        default=DEFAULT_PID,
        help="USB product ID used for auto-detect. Default: 0x0009",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="Serial read timeout in seconds. Default: 5",
    )
    parser.add_argument(
        "--expected-jedec",
        type=parse_jedec_id,
        default=DEFAULT_JEDEC_ID,
        help="Expected flash JEDEC ID as hex bytes. Default: EF4015",
    )
    parser.add_argument(
        "--no-boot",
        action="store_true",
        help="Program and verify flash, but do not release CRESET or check CDONE.",
    )
    return parser.parse_args()


def parse_hex_int(value):
    """Accept both decimal values and prefixed hex values such as 0x2E8A."""

    return int(value, 0)


def parse_jedec_id(value):
    """Parse a 3-byte JEDEC ID written as EF4015, EF:40:15, or EF 40 15."""

    cleaned = value.replace(":", "").replace(" ", "").replace("-", "")
    if len(cleaned) != 6:
        raise argparse.ArgumentTypeError("JEDEC ID must be exactly 3 bytes")
    try:
        return bytes.fromhex(cleaned)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("JEDEC ID must be hexadecimal") from exc


def find_board(vid, pid):
    """Return the first serial device matching the Pico VID/PID pair."""

    for port in serial.tools.list_ports.comports():
        if port.vid == vid and port.pid == pid:
            return port.device
    return None


def send_command(ser, command, response_len=0):
    """Send one fixed-width firmware command and validate its echo.

    The firmware echoes every 8-byte command before returning command-specific
    data. Checking that echo catches desynchronization before any binary
    response is interpreted as flash data.
    """

    if isinstance(command, str):
        command = command.encode("ascii")
    if len(command) != 8:
        raise ProgrammerError(f"Command must be 8 bytes, got {command!r}")

    ser.write(command)
    echo = read_exact(ser, 8, f"{command.decode('ascii')} echo")
    if echo != command:
        raise ProgrammerError(f"Unexpected echo for {command!r}: {echo!r}")

    if response_len == 0:
        return b""
    return read_exact(ser, response_len, command.decode("ascii"))


def read_exact(ser, size, label):
    """Read an exact number of bytes or fail with a useful timeout message."""

    data = ser.read(size)
    if len(data) != size:
        raise ProgrammerError(
            f"Timed out reading {label}: expected {size} byte(s), got {len(data)}"
        )
    return data


def read_board_info(ser):
    """Confirm that the connected serial device is running FrostByte firmware."""

    board_id = send_command(ser, "READINFO", 16).decode(
        "ascii", errors="replace"
    )
    print(f"Programmer ID: {board_id}")
    if board_id != EXPECTED_BOARD_ID:
        raise ProgrammerError(
            f"Expected {EXPECTED_BOARD_ID}; reflash the Pico with FrostByte firmware"
        )


def check_flash_id(ser, expected_jedec):
    """Reset the flash chip and confirm that its JEDEC ID is the expected one."""

    send_command(ser, "RESETCHP")
    time.sleep(0.1)

    jedec = send_command(ser, "DEVIDSST", 3)
    print(f"Flash JEDEC ID: {jedec.hex(' ').upper()}")
    if jedec != expected_jedec:
        raise ProgrammerError(
            "Unexpected flash JEDEC ID: "
            f"expected {expected_jedec.hex(' ').upper()}, got {jedec.hex(' ').upper()}"
        )


def hold_fpga(ser):
    """Assert CRESET before touching flash so the FPGA does not drive the bus."""

    result = send_command(ser, "HOLDFPGA", 1)
    if result != b"\x00":
        raise ProgrammerError(f"Unexpected HOLDFPGA response: {result!r}")
    print("FPGA held in reset: CRESET low")


def boot_fpga(ser):
    """Release CRESET and require CDONE to read high after configuration."""

    result = send_command(ser, "BOOTFPGA", 1)
    if result == b"\x01":
        print("FPGA boot confirmed: CDONE is high")
    elif result == b"\x00":
        raise ProgrammerError("FPGA boot failed: CDONE is low")
    else:
        raise ProgrammerError(f"Unexpected BOOTFPGA status byte: {result!r}")


def flash_file(ser, rom_path):
    """Erase, program, and read-back verify the bytes in rom_path."""

    filedata = read_rom(rom_path)
    filesize = len(filedata)

    num_blocks = div_round_up(filesize, BLOCK_SIZE)
    num_sectors = div_round_up(filesize, SECTOR_SIZE)

    if num_blocks > 0x100:
        raise ProgrammerError("ROM is too large for 8-bit ERASBK block addresses")
    if num_sectors > 0x100:
        raise ProgrammerError("ROM is too large for 8-bit WRSECT sector addresses")

    print(
        f"Flashing {filesize} bytes "
        f"({num_blocks} block(s), {num_sectors} sector(s))"
    )

    send_command(ser, "RESETCHP")
    time.sleep(0.1)

    erase_blocks(ser, num_blocks)
    write_sectors(ser, filedata, num_sectors)
    verify_sectors(ser, filedata, num_sectors)

    print("Flash and verification completed successfully")


def erase_blocks(ser, num_blocks):
    """Erase each affected 64 KiB block, then verify that it reads as blank."""

    for block in range(num_blocks):
        status = send_command(ser, f"ERASBK{block:02X}", 1)
        if status != bytes([block]):
            raise ProgrammerError(
                f"Erase block {block:02X} failed: got {status!r}"
            )
        print(f"Erased block {block:02X}")

    for block in range(num_blocks):
        status = send_command(ser, f"CHCKBK{block:02X}", 1)
        if status != b"\x00":
            raise ProgrammerError(f"Erase verify failed for block {block:02X}")
        print(f"Verified erased block {block:02X}")


def write_sectors(ser, filedata, num_sectors):
    """Write each 4 KiB sector and validate the firmware's CRC16 response."""

    for sector in range(num_sectors):
        chunk = padded_sector(filedata, sector)
        command = f"WRSECT{sector:02X}".encode("ascii")

        ser.write(command)
        echo = read_exact(ser, 8, f"WRSECT{sector:02X} echo")
        if echo != command:
            raise ProgrammerError(f"Unexpected echo for {command!r}: {echo!r}")

        ser.write(chunk)
        crc_recv = read_exact(ser, 2, f"WRSECT{sector:02X} CRC")
        crc_expected = crc16(chunk)
        if int.from_bytes(crc_recv, "little") != crc_expected:
            raise ProgrammerError(
                f"CRC mismatch on sector {sector:02X}: "
                f"expected {crc_expected:04X}, got {int.from_bytes(crc_recv, 'little'):04X}"
            )
        print(f"Wrote sector {sector:02X}")


def verify_sectors(ser, filedata, num_sectors):
    """Read every written page back from flash and compare it byte-for-byte."""

    print("Verifying written data...")

    for sector in range(num_sectors):
        expected = padded_sector(filedata, sector)

        for page in range(PAGES_PER_SECTOR):
            page_index = sector * PAGES_PER_SECTOR + page
            readback = send_command(ser, f"RDPG{page_index:04X}", PAGE_SIZE)
            expected_page = expected[page * PAGE_SIZE : (page + 1) * PAGE_SIZE]
            if readback != expected_page:
                offset = first_difference(readback, expected_page)
                address = page_index * PAGE_SIZE + offset
                raise ProgrammerError(
                    f"Verification failed at flash address 0x{address:06X} "
                    f"(page {page_index:04X}, byte {offset:02X})"
                )

        print(f"Verified sector {sector:02X}")


def padded_sector(filedata, sector):
    """Return one sector, padding the final partial sector with erased bytes."""

    start = sector * SECTOR_SIZE
    end = start + SECTOR_SIZE
    chunk = bytes(filedata[start:end])
    if len(chunk) < SECTOR_SIZE:
        chunk += bytes([0xFF]) * (SECTOR_SIZE - len(chunk))
    return chunk


def first_difference(left, right):
    """Find the first byte offset where two buffers differ."""

    for index, (a_byte, b_byte) in enumerate(zip(left, right)):
        if a_byte != b_byte:
            return index
    return min(len(left), len(right))


def div_round_up(value, divisor):
    """Return ceil(value / divisor) using integer arithmetic."""

    return (value + divisor - 1) // divisor


def crc16(data):
    """Compute CRC16/XMODEM, matching the checksum returned by the firmware."""

    crc = 0
    poly = 0x1021
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc <<= 1
            if crc & 0x10000:
                crc ^= poly
            crc &= 0xFFFF
    return crc


def read_rom(filename):
    """Read a ROM or bitstream file as bytes."""

    with open(filename, "rb") as f:
        return bytearray(f.read())


if __name__ == "__main__":
    try:
        main()
    except ProgrammerError as exc:
        raise SystemExit(f"Error: {exc}") from exc
