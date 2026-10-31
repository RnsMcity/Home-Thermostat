#!/usr/bin/env python3
# Convert a raw framebuffer dump from the STM32F469's SDRAM into an image.
#
# The LTDC scans ARGB8888 (memory byte order B,G,R,A) out of SDRAM:
#   FB1 @ 0xC0000000, FB2 @ 0xC0180000, 800x480x4 = 1,536,000 bytes each.
#
# Dump with (the CPU MUST stay halted for the whole dump — the UI redraws at
# ~1 Hz and an unhalted target yields torn frames):
#   openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
#       -c "transport select swd" -c "init" -c "halt" \
#       -c "dump_image fb1.bin 0xC0000000 0x177000" \
#       -c "dump_image fb2.bin 0xC0180000 0x177000" \
#       -c "resume" -c "shutdown"
# Notes: LTDC keeps scanning during the halt, so SWD reads of the displayed
# buffer contend and are very slow (~3 min — let it finish). Both FBs matching
# byte-for-byte means the halt landed between redraws and the frame is clean;
# if one shows sliced/partial digits, halt again and retry.
#
# Then:
#   python3 tools/capture_screen.py fb1.bin mockup-ui.jpg
import sys
from PIL import Image

WIDTH, HEIGHT = 800, 480
BYTES_PP = 4

def main():
    if len(sys.argv) < 3:
        sys.exit(f"usage: {sys.argv[0]} <fb.dump> <out.png|.jpg> [offset]")
    src, dst = sys.argv[1], sys.argv[2]
    offset = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0

    size = WIDTH * HEIGHT * BYTES_PP
    with open(src, "rb") as f:
        f.seek(offset)
        data = f.read(size)
    if len(data) != size:
        sys.exit(f"short read: need {size} bytes at offset {offset}, got {len(data)}")

    img = Image.frombytes("RGBA", (WIDTH, HEIGHT), data, "raw", "BGRA")
    if dst.lower().endswith((".jpg", ".jpeg")):
        bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
        bg.alpha_composite(img)
        bg.convert("RGB").save(dst, quality=92)
    else:
        img.save(dst)
    print(f"wrote {dst}")

if __name__ == "__main__":
    main()
