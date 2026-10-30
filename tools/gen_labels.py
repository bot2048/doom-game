#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generates the touch-button labels in resources/images/.

The app runs in landscape on a portrait screen, and the KeyOS Slint renderer
cannot rotate text, so every label is drawn here already rotated 90 degrees
clockwise. Rerun after changing a label:

    python3 tools/gen_labels.py
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent.parent / "resources" / "images"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
INK = (242, 230, 216, 255)

# name -> (text, font size in px)
TEXT_LABELS = {
    "lbl-menu": ("MENU", 20),
    "lbl-map": ("MAP", 20),
    "lbl-enter": ("ENTER", 20),
    "lbl-wpn-prev": ("WPN -", 20),
    "lbl-wpn-next": ("WPN +", 20),
    "lbl-fire": ("FIRE", 30),
    "lbl-use": ("USE", 22),
    "lbl-strafe-l": ("«", 34),
    "lbl-strafe-r": ("»", 34),
}

# Full-screen messages: name -> (text, font size in px)
MESSAGES = {
    "msg-no-wad": (
        "No WAD file found.\n\n"
        "Connect Passport to your computer, copy\n"
        "freedoom1.wad (or doom1.wad) onto the\n"
        "Airlock drive, then open DOOM again.",
        24,
    ),
}


def text_label(text, size):
    font = ImageFont.truetype(FONT, size)
    probe = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    left, top, right, bottom = (int(v) for v in probe.multiline_textbbox((0, 0), text, font=font, align="center"))
    img = Image.new("RGBA", (right - left + 4, bottom - top + 4), (0, 0, 0, 0))
    ImageDraw.Draw(img).multiline_text((2 - left, 2 - top), text, font=font, fill=INK, align="center")
    return img


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    labels = {name: text_label(*spec) for name, spec in TEXT_LABELS.items()}
    labels |= {name: text_label(*spec) for name, spec in MESSAGES.items()}
    for name, img in labels.items():
        # Landscape "up" is the portrait screen's right edge: rotate clockwise.
        img.transpose(Image.Transpose.ROTATE_270).save(OUT / f"{name}.png")
        print(f"wrote {name}.png {img.size[1]}x{img.size[0]}")


if __name__ == "__main__":
    main()
