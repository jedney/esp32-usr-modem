"""Render cells captured from the actual native terminal emulator tests.

Set USR_UI_SNAPSHOTS=1 and run native/test_config after creating .pio/ui-snapshots.
Then run this helper with Pillow installed. These are runtime verification images,
not production assets or a replacement for testing bytes/keys/services.
"""
import argparse
import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]


def font_path():
    choices = [Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/consola.ttf",
               Path("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf")]
    return next((str(path) for path in choices if path.exists()), None)


def render(path, font):
    header, body = path.read_bytes().split(b"\n", 1)
    cols, rows = map(int, header.split())
    if not (20 <= cols <= 132 and 8 <= rows <= 50) or len(body) != cols * rows * 4:
        raise ValueError(f"Invalid cell capture: {path}")
    width, height, margin = 12, 23, 18
    image = Image.new("RGB", (cols * width + 2 * margin, rows * height + 2 * margin), "#030507")
    draw = ImageDraw.Draw(image)
    for n in range(cols * rows):
        glyph, r, g, b = body[n * 4:n * 4 + 4]
        text = bytes([glyph]).decode("cp437")
        x, y = margin + (n % cols) * width, margin + (n // cols) * height
        # Console line/block cells fill their grid, avoiding font-metric seams.
        if glyph == 219:
            draw.rectangle((x, y, x + width - 1, y + height - 1), fill=(r, g, b))
        else:
            draw.text((x, y), text, font=font, fill=(r, g, b))
    out = path.with_suffix(".png")
    image.save(out)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", default=font_path())
    parser.add_argument("directory", nargs="?", type=Path, default=ROOT / ".pio/ui-snapshots")
    args = parser.parse_args()
    if not args.font:
        parser.error("Supply a fixed-width font with --font")
    font = ImageFont.truetype(args.font, 18)
    for path in sorted(args.directory.glob("*.cells")):
        print(render(path, font))


if __name__ == "__main__":
    main()
