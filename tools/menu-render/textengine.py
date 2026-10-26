"""Text in the game's Korean UI font, approximated with Malgun Gothic.

The magic menu draws its text in "$EverywhereMediumFont", which TAKEALOOK maps to "Futura Book BT" inside
Interface/fonts_futura.swf (TAKEALOOK - Font Edit/Interface/fontconfig.txt). That SWF font is not usable from
Python, so C:/Windows/Fonts/malgun.ttf is drawn instead, scaled per character class so its ink size matches the
game font measured on the build-3 frame base_v3_poison.png (see README "Font calibration"):

  Hangul: ink height 0.842 x Malgun's at the same size, ink width 0.636 x  ->  em 0.842*S, squeeze 0.755
  Other : ink height 0.72 x, ink width 0.761 x                              ->  em 0.72*S,  squeeze 1.057

Sizes and coordinates are stage units; drawing happens on a canvas of `ss` pixels per stage unit.
"""
from __future__ import annotations

import logging
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

log = logging.getLogger(__name__)

FONT_PATH = Path("C:/Windows/Fonts/malgun.ttf")
BOLD_FONT_PATH = Path("C:/Windows/Fonts/malgunbd.ttf")  # only for copying the SkyUI item card's bolder text
HANGUL_EM, HANGUL_SX = 0.842, 0.755
OTHER_EM, OTHER_SX = 0.72, 1.057
GUTTER = 2.0          # AS2 text fields keep 2 units free on each side (Widget.cpp:86 kGutter = 4 for both sides)
BASELINE = 0.90       # first baseline below the field's text top, per size unit (measured: size 19 -> 17 units)
LINE_PITCH = 1.20     # drawn line pitch per size unit (Widget.cpp:85 lays out with 1.25; textHeight read back is less)
MEASURE_PX = 64       # font pixel size used for width measurement


def is_hangul(ch: str) -> bool:
    o = ord(ch)
    return 0xAC00 <= o <= 0xD7A3 or 0x3130 <= o <= 0x318F or 0x1100 <= o <= 0x11FF


@lru_cache(maxsize=64)
def _font(px: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    path = BOLD_FONT_PATH if bold else FONT_PATH
    if not path.exists():
        raise FileNotFoundError(f"Korean font not found at {path}; set textengine.FONT_PATH / BOLD_FONT_PATH to a TTF with Hangul")
    return ImageFont.truetype(str(path), px)


def runs(text: str) -> list[tuple[str, bool]]:
    """Splits text into (run, hangul) pieces; a space joins the run it is in."""
    out: list[tuple[str, bool]] = []
    for ch in text:
        h = is_hangul(ch) if ch != " " else (out[-1][1] if out else False)
        if out and out[-1][1] == h:
            out[-1] = (out[-1][0] + ch, h)
        else:
            out.append((ch, h))
    return out


@dataclass(frozen=True)
class Style:
    """A text style: game size plus extra horizontal stretch (1.0 for our widget's own fields)."""
    size: float
    stretch: float = 1.0
    bold: bool = False


def width(text: str, style: Style) -> float:
    """Advance width of one line in stage units (what AS2 textWidth reports for a single line)."""
    total = 0.0
    for run, hangul in runs(text):
        em, sx = (HANGUL_EM, HANGUL_SX) if hangul else (OTHER_EM, OTHER_SX)
        total += _font(MEASURE_PX, style.bold).getlength(run) / MEASURE_PX * em * sx * style.size
    return total * style.stretch


def wrap(text: str, style: Style, max_w: float) -> list[str]:
    """AS2 wordWrap: CR splits lines; greedy at spaces; a word wider than the line breaks by characters."""
    lines: list[str] = []
    for para in text.split("\r"):
        words = para.split(" ")
        cur = ""
        for word in words:
            trial = word if not cur else cur + " " + word
            if width(trial, style) <= max_w or not cur and width(word, style) <= max_w:
                cur = trial
                continue
            if cur:
                lines.append(cur)
            cur = ""
            piece = ""
            for ch in word:
                if width(piece + ch, style) <= max_w or not piece:
                    piece += ch
                else:
                    lines.append(piece)
                    piece = ch
            cur = piece
        lines.append(cur)
    return lines


def text_size(text: str, style: Style, field_w: float, word_wrap: bool) -> tuple[float, float, list[str]]:
    """(textWidth, textHeight, lines) as AS2 would report them for a field of field_w."""
    inner = field_w - 2 * GUTTER
    lines = wrap(text, style, inner) if word_wrap else text.split("\r")
    tw = max((width(l, style) for l in lines), default=0.0)
    th = len(lines) * LINE_PITCH * style.size
    return tw, th, lines


def draw_line(canvas: Image.Image, x: float, baseline: float, text: str, style: Style, rgb: int, ss: float,
              clip: tuple[float, float, float, float] | None = None) -> None:
    """Draws one line starting at stage x on the stage baseline, onto a canvas of ss px per unit."""
    color = ((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255)
    pen = x
    for run, hangul in runs(text):
        em, sx = (HANGUL_EM, HANGUL_SX) if hangul else (OTHER_EM, OTHER_SX)
        px = max(1, round(style.size * em * ss))
        font = _font(px, style.bold)
        ascent, descent = font.getmetrics()
        adv = font.getlength(run)
        w_px = max(1, int(adv) + px)
        mask = Image.new("L", (w_px, ascent + descent + 2), 0)
        ImageDraw.Draw(mask).text((0, ascent), run, font=font, fill=255, anchor="ls")
        squeeze = sx * style.stretch
        new_w = max(1, round(w_px * squeeze))
        mask = mask.resize((new_w, mask.height), Image.LANCZOS)
        left = round(pen * ss)
        top = round(baseline * ss) - ascent
        if clip:
            cx0, cy0, cx1, cy1 = (round(v * ss) for v in clip)
            box = (max(left, cx0), max(top, cy0), min(left + mask.width, cx1), min(top + mask.height, cy1))
            if box[2] <= box[0] or box[3] <= box[1]:
                pen += adv / ss * squeeze
                continue
            mask = mask.crop((box[0] - left, box[1] - top, box[2] - left, box[3] - top))
            left, top = box[0], box[1]
        solid = Image.new("RGB", mask.size, color)
        canvas.paste(solid, (left, top), mask)
        pen += adv / ss * squeeze


def ink_box(text: str, style: Style, ss: float = 8.0) -> tuple[float, float, float, float]:
    """Ink bounds (x0, y0, x1, y1) in stage units relative to the pen start and baseline."""
    w = width(text, style) + style.size * 2
    img = Image.new("RGB", (int(w * ss) + 8, int(style.size * 3 * ss)), 0)
    draw_line(img, 0.0, style.size * 2, text, style, 0xFFFFFF, ss)
    bbox = img.convert("L").point(lambda v: 255 if v > 60 else 0).getbbox()
    if not bbox:
        return 0.0, 0.0, 0.0, 0.0
    return bbox[0] / ss, bbox[1] / ss - style.size * 2, bbox[2] / ss, bbox[3] / ss - style.size * 2


def fit_style(text: str, ink_w: float, ink_h: float, bold: bool = False) -> Style:
    """A style whose ink for `text` measures ink_w x ink_h (used to copy the real item card's text styles)."""
    x0, y0, x1, y1 = ink_box(text, Style(10.0, 1.0, bold))
    size = 10.0 * ink_h / max(1e-6, y1 - y0)
    x0, y0, x1, y1 = ink_box(text, Style(size, 1.0, bold))
    return Style(size, ink_w / max(1e-6, x1 - x0), bold)
