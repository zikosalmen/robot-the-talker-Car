#!/usr/bin/env python3
"""Slice the desktop-robot emotion sprite sheet into 21 landscape PNGs.

The ST7789 is configured with DISPLAY_INVERT_COLOR, so LVGL black becomes
white on glass and teal becomes red. Files written for firmware are
pre-inverted. _preview_on_glass.png is what you should see on the LCD.

RGB565 C arrays from scripts/Image_Converter/LVGLImage.py are ~150 KB each
(21 x 320 x 240 x 2 = 3.1 MB) and will not fit the app partition. Keep PNG.
"""

from __future__ import annotations

import math
import os
import sys

from PIL import Image, ImageDraw

NAMES = [
    "neutral",
    "happy",
    "laughing",
    "funny",
    "sad",
    "angry",
    "crying",
    "loving",
    "embarrassed",
    "surprised",
    "shocked",
    "thinking",
    "winking",
    "cool",
    "relaxed",
    "delicious",
    "kissy",
    "confident",
    "sleepy",
    "silly",
    "confused",
]

COLS = 6
ROWS = 4
OUT_W = 320
OUT_H = 240
# Drop labels (bottom of the tile). Do not rectangle-mask the top-right:
# that cut a notch out of tall right eyes (neutral, surprised, …).
INSET_LEFT = 0.04
INSET_RIGHT = 0.04
INSET_TOP = 0.08
INSET_BOTTOM = 0.32
EMOJI_REGION_X0 = 0.72
EMOJI_REGION_Y1 = 0.38
# Allow a 2x nearest upscale of the native eye crop (crisp, no LANCZOS blur).
MARGIN_X = 0.12
MARGIN_Y = 0.14
# Color the LCD should show after the panel invert.
GLASS_BG = (48, 50, 54)
PANEL_INVERT = True


def project_root() -> str:
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def default_sheet() -> str:
    return os.path.join(project_root(), "main", "assets", "faces", "_source_sheet.png")


def default_out_dir() -> str:
    return os.path.join(project_root(), "main", "assets", "faces")


def tile_box(width: int, height: int, index: int) -> tuple[int, int, int, int]:
    col = index % COLS
    row = index // COLS
    cell_w = width / COLS
    cell_h = height / ROWS
    x0 = col * cell_w
    y0 = row * cell_h
    inner_x0 = x0 + cell_w * INSET_LEFT
    inner_y0 = y0 + cell_h * INSET_TOP
    inner_x1 = x0 + cell_w * (1.0 - INSET_RIGHT)
    inner_y1 = y0 + cell_h * (1.0 - INSET_BOTTOM)
    return (
        int(round(inner_x0)),
        int(round(inner_y0)),
        int(round(inner_x1)),
        int(round(inner_y1)),
    )


def is_reference_emoji(pixel: tuple[int, int, int]) -> bool:
    """Corner emoji is yellow/cream. Teal eyes are G-dominant with high B."""
    r, g, b = pixel
    if max(r, g, b) < 90:
        return False
    if g > r + 15 and b > 90:
        return False  # teal / cyan
    if r > 110 and b > 80 and r > g and b + 20 >= g:
        return False  # pink hearts / blush
    # yellow / cream / orange / white of the tiny reference emoji
    if r > 130 and g > 90 and b < g - 8:
        return True
    if r > 150 and g > 80 and b < 110 and r > b + 30:
        return True
    if r > 160 and g > 160 and b > 140 and abs(r - g) < 40:
        return True
    return False


def mask_tile_chrome(sheet: Image.Image, index: int) -> None:
    """Remove captions and the yellow corner emoji without clipping the right eye."""
    col = index % COLS
    row = index // COLS
    cell_w = sheet.width / COLS
    cell_h = sheet.height / ROWS
    x0 = col * cell_w
    y0 = row * cell_h
    pixels = sheet.load()
    x1 = int(round(x0 + cell_w))
    y1 = int(round(y0 + cell_h))
    emoji_x0 = int(round(x0 + cell_w * EMOJI_REGION_X0))
    emoji_y1 = int(round(y0 + cell_h * EMOJI_REGION_Y1))
    caption_y0 = int(round(y0 + cell_h * (1.0 - INSET_BOTTOM)))
    for y in range(int(round(y0)), min(emoji_y1, sheet.height)):
        for x in range(max(0, emoji_x0), min(x1, sheet.width)):
            if is_reference_emoji(pixels[x, y]):
                pixels[x, y] = (0, 0, 0)
    for y in range(caption_y0, y1):
        for x in range(int(round(x0)), x1):
            if 0 <= x < sheet.width and 0 <= y < sheet.height:
                pixels[x, y] = (0, 0, 0)


def is_eye_paint(pixel: tuple[int, int, int]) -> bool:
    """True for teal/cyan, angry red, pink hearts/blush — not yellow emoji."""
    r, g, b = pixel
    if pixel == (0, 0, 0):
        return False
    if g >= r + 8 and g > 70:
        return True
    if r >= g + 20 and r > 70:
        return True
    if r > 100 and b > 70 and r > g:
        return True
    return False


def wipe_corner_junk(face: Image.Image) -> Image.Image:
    """The sheet's tiny emoji sits on the right eye's top-right. Keep eye paint only."""
    rgb = face.convert("RGB")
    w, h = rgb.size
    pixels = rgb.load()
    x0 = int(w * 0.68)
    y1 = int(h * 0.32)
    for y in range(0, y1):
        for x in range(x0, w):
            if pixels[x, y] != (0, 0, 0) and not is_eye_paint(pixels[x, y]):
                pixels[x, y] = (0, 0, 0)
    return rgb


def erase_stray_blobs(face: Image.Image) -> Image.Image:
    """Drop leftover corner-emoji specks. Keep eyes, tears, blush, motion marks."""
    rgb = face.convert("RGB")
    w, h = rgb.size
    pixels = rgb.load()
    seen = [[False] * w for _ in range(h)]
    blobs: list[list[tuple[int, int]]] = []
    for y in range(h):
        for x in range(w):
            if seen[y][x] or pixels[x, y] == (0, 0, 0):
                continue
            stack = [(x, y)]
            seen[y][x] = True
            blob: list[tuple[int, int]] = []
            while stack:
                cx, cy = stack.pop()
                blob.append((cx, cy))
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < w and 0 <= ny < h and not seen[ny][nx]:
                        if pixels[nx, ny] != (0, 0, 0):
                            seen[ny][nx] = True
                            stack.append((nx, ny))
            blobs.append(blob)
    if not blobs:
        return rgb
    total = sum(len(blob) for blob in blobs)
    for blob in blobs:
        xs = [p[0] for p in blob]
        ys = [p[1] for p in blob]
        cx = sum(xs) / len(xs)
        cy = sum(ys) / len(ys)
        in_corner = cx > w * 0.70 and cy < h * 0.28
        tiny = len(blob) < 12 or (in_corner and len(blob) < max(18, total // 25))
        if tiny:
            for bx, by in blob:
                pixels[bx, by] = (0, 0, 0)
    return rgb


def is_tile_background(pixel: tuple[int, int, int]) -> bool:
    """The sheet tiles are dark blue-gray (~19,24,30), not pure black."""
    r, g, b = pixel
    mx = max(r, g, b)
    chroma = mx - min(r, g, b)
    if mx <= 42 and chroma <= 22:
        return True
    if mx <= 58 and chroma <= 14:
        return True
    return False


def flatten_background(face: Image.Image) -> Image.Image:
    rgb = face.convert("RGB")
    pixels = rgb.load()
    for y in range(rgb.height):
        for x in range(rgb.width):
            if is_tile_background(pixels[x, y]):
                pixels[x, y] = (0, 0, 0)
    return rgb


def content_bbox(face: Image.Image) -> tuple[int, int, int, int] | None:
    pixels = face.load()
    min_x, min_y = face.width, face.height
    max_x, max_y = -1, -1
    for y in range(face.height):
        for x in range(face.width):
            if pixels[x, y] != (0, 0, 0):
                if x < min_x:
                    min_x = x
                if y < min_y:
                    min_y = y
                if x > max_x:
                    max_x = x
                if y > max_y:
                    max_y = y
    if max_x < 0:
        return None
    pad = 2
    return (
        max(0, min_x - pad),
        max(0, min_y - pad),
        min(face.width, max_x + 1 + pad),
        min(face.height, max_y + 1 + pad),
    )


def invert_rgb(image: Image.Image) -> Image.Image:
    return image.point(lambda value: 255 - value)


def drop_fringe(face: Image.Image) -> Image.Image:
    """Keep saturated paint only. Anti-aliased gray/teal ramps become black."""
    rgb = face.convert("RGB")
    pixels = rgb.load()
    for y in range(rgb.height):
        for x in range(rgb.width):
            pixel = pixels[x, y]
            if pixel == (0, 0, 0) or is_tile_background(pixel) or is_reference_emoji(pixel):
                pixels[x, y] = (0, 0, 0)
                continue
            r, g, b = pixel
            chroma = max(r, g, b) - min(r, g, b)
            if chroma < 36 or max(r, g, b) < 90:
                pixels[x, y] = (0, 0, 0)
    return rgb


def _connected_blobs(rgb: Image.Image) -> list[list[tuple[int, int]]]:
    w, h = rgb.size
    pixels = rgb.load()
    seen = [[False] * w for _ in range(h)]
    blobs: list[list[tuple[int, int]]] = []
    for y in range(h):
        for x in range(w):
            if seen[y][x] or pixels[x, y] == (0, 0, 0):
                continue
            stack = [(x, y)]
            seen[y][x] = True
            blob: list[tuple[int, int]] = []
            while stack:
                cx, cy = stack.pop()
                blob.append((cx, cy))
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < w and 0 <= ny < h and not seen[ny][nx]:
                        if pixels[nx, ny] != (0, 0, 0):
                            seen[ny][nx] = True
                            stack.append((nx, ny))
            blobs.append(blob)
    return blobs


def _convex_hull(points: list[tuple[int, int]]) -> list[tuple[int, int]]:
    pts = sorted(set(points))
    if len(pts) <= 2:
        return pts
    def cross(origin: tuple[int, int], a: tuple[int, int], b: tuple[int, int]) -> int:
        return (a[0] - origin[0]) * (b[1] - origin[1]) - (a[1] - origin[1]) * (b[0] - origin[0])
    lower: list[tuple[int, int]] = []
    for point in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], point) <= 0:
            lower.pop()
        lower.append(point)
    upper: list[tuple[int, int]] = []
    for point in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], point) <= 0:
            upper.pop()
        upper.append(point)
    return lower[:-1] + upper[:-1]


def _blob_fill_color(blob: list[tuple[int, int]], pixels) -> tuple[int, int, int]:
    inner: list[tuple[int, int, int]] = []
    for x, y in blob:
        r, g, b = pixels[x, y]
        chroma = max(r, g, b) - min(r, g, b)
        if chroma >= 40 and max(r, g, b) >= 100:
            inner.append((r, g, b))
    samples = inner or [pixels[x, y] for x, y in blob]
    samples.sort()
    return samples[len(samples) // 2]


def paint_solid_blobs(face: Image.Image) -> Image.Image:
    """One flat color per blob. Convex-fill chipped ovals (neutral right eye)."""
    rgb = face.convert("RGB")
    out = Image.new("RGB", rgb.size, (0, 0, 0))
    draw = ImageDraw.Draw(out)
    pixels = rgb.load()
    for blob in _connected_blobs(rgb):
        if len(blob) < 4:
            continue
        color = _blob_fill_color(blob, pixels)
        hull = _convex_hull(blob)
        hull_area = len(blob)
        if len(hull) >= 3:
            mask = Image.new("1", rgb.size, 0)
            ImageDraw.Draw(mask).polygon(hull, fill=1)
            hull_area = sum(mask.getdata()) or len(blob)
        solidity = len(blob) / hull_area
        if len(blob) >= 80 and solidity >= 0.93 and len(hull) >= 3:
            draw.polygon(hull, fill=color)
        else:
            out_px = out.load()
            for x, y in blob:
                out_px[x, y] = color
    return out


def _fit_ellipse(blob: list[tuple[int, int]]) -> tuple[float, float, float, float, float]:
    n = float(len(blob))
    cx = sum(p[0] for p in blob) / n
    cy = sum(p[1] for p in blob) / n
    cxx = sum((p[0] - cx) ** 2 for p in blob) / n
    cyy = sum((p[1] - cy) ** 2 for p in blob) / n
    cxy = sum((p[0] - cx) * (p[1] - cy) for p in blob) / n
    trace = cxx + cyy
    det = cxx * cyy - cxy * cxy
    disc = max(0.0, (trace / 2.0) ** 2 - det)
    root = math.sqrt(disc)
    lam1 = max(0.0, trace / 2.0 + root)
    lam2 = max(0.0, trace / 2.0 - root)
    # Uniform filled ellipse: variance along an axis is r^2 / 4.
    rx = max(1.0, 2.0 * math.sqrt(lam1))
    ry = max(1.0, 2.0 * math.sqrt(lam2))
    angle = 0.5 * math.atan2(2.0 * cxy, cxx - cyy)
    return cx, cy, rx, ry, angle


def _blit_ellipse(
    image: Image.Image,
    cx: float,
    cy: float,
    rx: float,
    ry: float,
    angle: float,
    color: tuple[int, int, int],
) -> None:
    pixels = image.load()
    cos_a = math.cos(angle)
    sin_a = math.sin(angle)
    ext_x = abs(rx * cos_a) + abs(ry * sin_a)
    ext_y = abs(rx * sin_a) + abs(ry * cos_a)
    x0 = max(0, int(math.floor(cx - ext_x)))
    x1 = min(image.width - 1, int(math.ceil(cx + ext_x)))
    y0 = max(0, int(math.floor(cy - ext_y)))
    y1 = min(image.height - 1, int(math.ceil(cy + ext_y)))
    inv_rx2 = 1.0 / (rx * rx)
    inv_ry2 = 1.0 / (ry * ry)
    for y in range(y0, y1 + 1):
        dy = y - cy
        for x in range(x0, x1 + 1):
            dx = x - cx
            local_x = dx * cos_a + dy * sin_a
            local_y = -dx * sin_a + dy * cos_a
            if local_x * local_x * inv_rx2 + local_y * local_y * inv_ry2 <= 1.0:
                pixels[x, y] = color


def fit_on_canvas(face: Image.Image) -> tuple[Image.Image, Image.Image]:
    """Return (on_glass, firmware) RGB images."""
    flat = paint_solid_blobs(
        erase_stray_blobs(drop_fringe(wipe_corner_junk(flatten_background(face))))
    )
    box = content_bbox(flat)
    eyes = flat.crop(box) if box else flat
    max_w = int(OUT_W * (1.0 - 2 * MARGIN_X))
    max_h = int(OUT_H * (1.0 - 2 * MARGIN_Y))
    scale = max(1, min(max_w // eyes.width, max_h // eyes.height))
    new_w = eyes.width * scale
    new_h = eyes.height * scale
    origin_x = (OUT_W - new_w) // 2
    origin_y = (OUT_H - new_h) // 2
    glass = Image.new("RGB", (OUT_W, OUT_H), GLASS_BG)
    glass_px = glass.load()
    ovals: list[tuple[list[tuple[int, int]], tuple[int, int, int], tuple[float, float, float, float, float]]] = []
    for blob in _connected_blobs(eyes):
        if len(blob) < 4:
            continue
        color = eyes.getpixel(blob[0])
        hull = _convex_hull(blob)
        hull_area = len(blob)
        if len(hull) >= 3:
            mask = Image.new("1", eyes.size, 0)
            ImageDraw.Draw(mask).polygon(hull, fill=1)
            hull_area = sum(mask.getdata()) or len(blob)
        solidity = len(blob) / hull_area
        oval = len(blob) >= 40 and solidity >= 0.93
        if oval:
            ovals.append((blob, color, _fit_ellipse(blob)))
        else:
            for x, y in blob:
                for dy in range(scale):
                    for dx in range(scale):
                        glass_px[origin_x + x * scale + dx, origin_y + y * scale + dy] = color
    if len(ovals) == 2:
        first, second = ovals
        left, right = (first, second) if first[2][0] <= second[2][0] else (second, first)
        area_l = left[2][2] * left[2][3]
        area_r = right[2][2] * right[2][3]
        similar = max(area_l, area_r) / max(1e-6, min(area_l, area_r)) < 1.25
        if similar:
            rx = max(left[2][2], right[2][2])
            ry = max(left[2][3], right[2][3])
            angle = left[2][4] if abs(left[2][4]) <= abs(right[2][4]) else right[2][4]
            if abs(angle) < 0.18:
                angle = 0.0
            ovals = [
                (left[0], left[1], (left[2][0], left[2][1], rx, ry, angle)),
                (right[0], right[1], (right[2][0], right[2][1], rx, ry, angle)),
            ]
    for blob, color, (cx, cy, rx, ry, angle) in ovals:
        if abs(angle) < 0.12:
            angle = 0.0
        _blit_ellipse(
            glass,
            origin_x + cx * scale,
            origin_y + cy * scale,
            rx * scale,
            ry * scale,
            angle,
            color,
        )
    firmware = invert_rgb(glass) if PANEL_INVERT else glass.copy()
    return glass, firmware


def slice_sheet(sheet_path: str, out_dir: str) -> list[str]:
    os.makedirs(out_dir, exist_ok=True)
    sheet = Image.open(sheet_path).convert("RGB")
    written = []
    preview = Image.new("RGB", (OUT_W * 6, OUT_H * 4), GLASS_BG)
    for i, name in enumerate(NAMES):
        mask_tile_chrome(sheet, i)
        box = tile_box(sheet.width, sheet.height, i)
        face = sheet.crop(box)
        glass, firmware = fit_on_canvas(face)
        path = os.path.join(out_dir, f"{name}.png")
        firmware.save(path, optimize=True)
        written.append(path)
        preview.paste(glass, ((i % COLS) * OUT_W, (i // COLS) * OUT_H))
        print(f"{name:12} crop={box} bytes={os.path.getsize(path)}")
    preview_path = os.path.join(out_dir, "_preview_on_glass.png")
    preview.save(preview_path, optimize=True)
    print(f"preview {preview_path} bytes={os.path.getsize(preview_path)}")
    total = sum(os.path.getsize(p) for p in written)
    print(f"21 faces total {total} bytes ({total / 1024:.1f} KiB)")
    return written


def main() -> int:
    sheet = sys.argv[1] if len(sys.argv) > 1 else default_sheet()
    out_dir = sys.argv[2] if len(sys.argv) > 2 else default_out_dir()
    if not os.path.isfile(sheet):
        print(f"missing sheet: {sheet}", file=sys.stderr)
        return 1
    slice_sheet(sheet, out_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
