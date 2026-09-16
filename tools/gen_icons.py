#!/usr/bin/env python3
"""Generate include/ui_icons.h for the thermostat UI.

Draws each icon in white on a transparent 256x256 canvas (4x supersampling),
downsamples to 64x64, and emits LVGL A8 alpha-mask images. Icons are recolored
at runtime with lv_image_set_recolor() for active/inactive states.

Usage:
    pip install pillow
    python3 tools/gen_icons.py            # writes include/ui_icons.h
"""

import math
from PIL import Image, ImageDraw

SS = 4                      # supersampling factor
SIZE = 64                   # final icon size (px)
CANVAS = SIZE * SS          # 256
OUT = "include/ui_icons.h"


def cubic_bezier(p0, p1, p2, p3, n=28):
    pts = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        x = u**3 * p0[0] + 3 * u**2 * t * p1[0] + 3 * u * t**2 * p2[0] + t**3 * p3[0]
        y = u**3 * p0[1] + 3 * u**2 * t * p1[1] + 3 * u * t**2 * p2[1] + t**3 * p3[1]
        pts.append((x, y))
    return pts


def new_canvas():
    return Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))


WHITE = (255, 255, 255, 255)
CLEAR = (0, 0, 0, 0)


def draw_flame():
    """Flame silhouette with a secondary tongue on the left and an inner cutout."""
    img = new_canvas()
    d = ImageDraw.Draw(img)

    # Outer: main tip, right bulge, rounded bottom, then up the left side
    # through a notch to a smaller secondary peak (the "tongue").
    tip  = (130, 12)
    rm   = (188, 130)
    bot  = (126, 240)
    lb   = (70, 170)
    notch= (98, 120)
    tt   = (74, 70)

    outer = []
    outer += cubic_bezier(tip, (152, 56), (184, 88), rm)
    outer += cubic_bezier(rm, (208, 178), (176, 232), bot)[1:]
    outer += cubic_bezier(bot, (76, 232), (58, 204), lb)[1:]
    outer += cubic_bezier(lb, (62, 148), (84, 132), notch)[1:]
    outer += cubic_bezier(notch, (112, 104), (88, 84), tt)[1:]
    outer += cubic_bezier(tt, (64, 44), (96, 20), tip)[1:-1]
    d.polygon(outer, fill=WHITE)

    # Inner cutout — small teardrop in the lower half
    itip = (132, 108)
    ir   = (168, 172)
    ibot = (128, 214)
    il   = (96, 172)

    inner = []
    inner += cubic_bezier(itip, (150, 132), (166, 150), ir)
    inner += cubic_bezier(ir, (184, 198), (162, 214), ibot)[1:]
    inner += cubic_bezier(ibot, (94, 214), (80, 198), il)[1:]
    inner += cubic_bezier(il, (90, 150), (114, 132), itip)[1:-1]
    d.polygon(inner, fill=CLEAR)
    return img


def draw_snowflake():
    """Six arms with a pair of V-branches on each."""
    img = new_canvas()
    d = ImageDraw.Draw(img)
    c = (CANVAS / 2, CANVAS / 2)
    arm_len = 112
    for k in range(6):
        a = math.radians(-90 + k * 60)
        ux, uy = math.cos(a), math.sin(a)

        def rot(vx, vy, deg):
            r = math.radians(deg)
            return (vx * math.cos(r) - vy * math.sin(r),
                    vx * math.sin(r) + vy * math.cos(r))

        d.line([(c[0] + 14 * ux, c[1] + 14 * uy),
                (c[0] + arm_len * ux, c[1] + arm_len * uy)], fill=WHITE, width=13)

        px = c[0] + 0.62 * arm_len * ux
        py = c[1] + 0.62 * arm_len * uy
        b = 0.34 * arm_len
        for sgn in (-145, 145):
            bx, by = rot(ux, uy, sgn)
            d.line([(px, py), (px + b * bx, py + b * by)], fill=WHITE, width=11)
    return img


def draw_fan():
    """Four-blade pinwheel with a small hub."""
    blade = new_canvas()
    bd = ImageDraw.Draw(blade)
    # narrow, long ellipse offset from center -> propeller swirl when rotated
    bd.ellipse([130, 8, 182, 128], fill=WHITE)

    img = new_canvas()
    for k in range(4):
        rot_img = blade.rotate(-k * 90, resample=Image.BICUBIC, center=(CANVAS / 2, CANVAS / 2))
        img.alpha_composite(rot_img)
    d = ImageDraw.Draw(img)
    d.ellipse([128 - 13, 128 - 13, 128 + 13, 128 + 13], fill=WHITE)
    return img


def draw_auto():
    """Open circular loop with a knob dot at each end."""
    img = new_canvas()
    d = ImageDraw.Draw(img)
    c = CANVAS / 2
    r = 84
    bbox = [c - r, c - r, c + r, c + r]

    start_deg, end_deg = -70, 230   # 300 deg sweep, gap near the top
    d.arc(bbox, start=start_deg, end=end_deg, fill=WHITE, width=15)

    def pt(deg):
        a = math.radians(deg)
        return (c + r * math.cos(a), c + r * math.sin(a))

    s = pt(start_deg)
    e = pt(end_deg)
    d.ellipse([s[0] - 16, s[1] - 16, s[0] + 16, s[1] + 16], fill=WHITE)
    d.ellipse([e[0] - 12, e[1] - 12, e[0] + 12, e[1] + 12], fill=WHITE)
    return img


def draw_chevron(up=True):
    """Thick chevron with rounded caps."""
    img = new_canvas()
    d = ImageDraw.Draw(img)
    if up:
        apex, left, right = (128, 56), (32, 184), (224, 184)
    else:
        apex, left, right = (128, 192), (32, 72), (224, 72)

    d.line([left, apex], fill=WHITE, width=36)
    d.line([apex, right], fill=WHITE, width=36)
    for p in (left, apex, right):
        d.ellipse([p[0] - 18, p[1] - 18, p[0] + 18, p[1] + 18], fill=WHITE)
    return img


ICONS = [
    ("flame", draw_flame),
    ("snowflake", draw_snowflake),
    ("fan", draw_fan),
    ("auto", draw_auto),
    ("chevron_up", lambda: draw_chevron(up=True)),
    ("chevron_down", lambda: draw_chevron(up=False)),
]


def to_a8(img):
    small = img.resize((SIZE, SIZE), Image.LANCZOS)
    return list(small.getchannel("A").getdata())


def emit_c(name, data):
    lines = []
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02X}" for b in data[i:i + 16])
        lines.append(f"    {chunk},")
    return "\n".join(lines)


def main():
    parts = []
    for name, fn in ICONS:
        data = to_a8(fn())
        parts.append((name, data))

    with open(OUT, "w") as f:
        f.write("#ifndef UI_ICONS_H\n#define UI_ICONS_H\n\n")
        f.write("/* GENERATED by tools/gen_icons.py — do not hand-edit. */\n\n")
        f.write('#include "lvgl.h"\n\n')
        for name, data in parts:
            f.write(f"static const uint8_t ui_icon_{name}_map[{len(data)}] = {{\n")
            f.write(emit_c(name, data))
            f.write("};\n\n")
        for name, _ in parts:
            f.write(f"static const lv_image_dsc_t ui_icon_{name} = {{\n")
            f.write("    .header.magic   = LV_IMAGE_HEADER_MAGIC,\n")
            f.write("    .header.cf      = LV_COLOR_FORMAT_A8,\n")
            f.write("    .header.flags   = 0,\n")
            f.write(f"    .header.w       = {SIZE},\n")
            f.write(f"    .header.h       = {SIZE},\n")
            f.write(f"    .header.stride  = {SIZE},\n")
            f.write("    .data_size      = sizeof(ui_icon_%s_map),\n" % name)
            f.write("    .data           = ui_icon_%s_map,\n" % name)
            f.write("};\n\n")
        f.write("#endif /* UI_ICONS_H */\n")

    print(f"wrote {OUT} ({len(parts)} icons, {SIZE}x{SIZE} A8)")


if __name__ == "__main__":
    main()
