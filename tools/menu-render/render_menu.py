"""Offline renderer: what SL_MagicMenu build 5 draws on the SkyUI magic menu, composed over a real game frame.

Usage (Python 3.10+, Pillow, numpy):
    python render_menu.py [--tree spell_tree.json] [--video3 DIR] [--out DIR] [--row-size 14]

Every layout constant below is copied from the plugin source (file:line in README.md). Nothing here is read by
the plugin; this is a picture of the code, for review without starting the game.
"""
from __future__ import annotations

import argparse
import colorsys
import logging
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

import menu_model as mm
import textengine as te
from menu_model import State
from textengine import Style

log = logging.getLogger("menu_render")

# ------------------------------------------------------------------------------------------------------------
# Constants (plugins/SL_MagicMenu/src, build 5)
# ------------------------------------------------------------------------------------------------------------

# Settings.h:44-58, dist/SL_MagicMenu.ini [Layout]
PREVIEW = (648, 67, 957, 330)
TREE = (971, 67, 1238, 330)
PROGRESS = (648, 339, 969, 441)
ITEM_CARD = (648, 450, 969, 623)
NODE_CARD = (979, 339, 1238, 623)
TREE_SCALE, MAX_STEPS, MAX_NODES = 0.35, 1, 40
PANEL_TITLE_H, PANEL_PAD, TREE_LEGEND_H = 22.0, 8.0, 18.0
KEY_NEXT, KEY_SET_TARGET = "[U]", "[G]"  # ini iNext=22, iSetTarget=34 -> Widget.cpp KeyName/KeyLabel

# Widget.cpp:77-114
EDGE_W, TITLE_H, PAD, TITLE_SIZE = 1.0, PANEL_TITLE_H, PANEL_PAD, 12.0
PANEL_FILL, PANEL_EDGE, TITLE_COLOR = 0x000000, 0x8A7A5A, 0xC8C0B0
PREVIEW_ALPHA, PANEL_ALPHA = 100.0, 85.0
LIST_ROW_FALLBACK, LIST_ROW_MIN, LIST_ROW_MAX = 14.0, 10.0, 20.0
LINE_FACTOR, GUTTER = 1.25, 4.0
NODE_R, HIT_R, RING_GAP, ARC_W = 7.0, 10.0, 3.0, 2.5
SELECT_R, SELECT_ARM = 15.0, 4.5
LABEL_W, LABEL_SIZE, LABEL_GAP, LABEL_LINES = 104.0, 10.0, 1.0, 2
DASH, DASH_GAP, LINE_W = 4.0, 3.0, 1.0
NOTE_SIZE, LEGEND_SIZE, LEGEND_ICON_R, LEGEND_TEXT_GAP = 10.0, 10.0, 5.0, 4.0
EXIT_ARROW, EXIT_INSET, EXIT_TEXT_W = 5.0, 7.0, 36.0
GOLD, BLUE, WHITE, LOCKED_EDGE = 0xD6B56A, 0x4A90D9, 0xF0F0F0, 0x6A6A6A
PADLOCK, EDGE_COLOR, LOCK_EDGE_COLOR, LABEL_COLOR, DIM_TEXT = 0xA0A0A0, 0x9A9A9A, 0x7A7A7A, 0xD8D8D8, 0x8A8A8A
CARD_TOP, SCHOOL_SIZE, TARGET_SIZE, BAR_GAP, BAR_H, XP_W, XP_SIZE, HINT_GAP, HINT_SIZE = 4.0, 11.0, 19.0, 2.0, 9.0, 120.0, 12.0, 4.0, 13.0
MESSAGE_LINES = 2
BAR_BACK = 0x202830
NAME_SIZE, SUB_SIZE, STATE_SIZE, STATE_ICON_R, STATE_TEXT_X, RULE_GAP = 19.0, 12.0, 14.0, 6.0, 22.0, 4.0
ROW_SIZE, MARK_SHARE, MARK_GAP, BOTTOM_RULE_FROM_BOTTOM, XP_FROM_BOTTOM = 13.0, 0.8, 4.0, 76.0, 70.0
BUTTON_FROM_BOTTOM, BUTTON_H, BUTTON_SIZE = 38.0, 28.0, 14.0
NAME_LINES, HINT_LINES = 2, 2
OK, NOT_YET, BUTTON_FILL, BUTTON_OFF = 0x6CC070, 0xE05050, 0x1E3A5A, 0x262626
# Draw.h:172 check/cross stroke
MARK_MIN_W, MARK_W_SHARE = 1.5, 6.0
# PreviewMath.h:23
PREVIEW_FILL = 0.92

# Widget.cpp:189-241 kStrings (build 5, Korean)
S = {
    "progress_title": "{school} 학습",
    "progress_target": "학습 대상: {spell} ({pct}%)",
    "progress_none_candidates": "학습 대상 없음 · 배울 수 있는 주문 {n}개 · {key} 다음 주문",
    "progress_none_nothing": "배울 수 있는 주문 없음 (선행 주문 미충족)",
    "progress_all_mastered": "모든 주문 숙달 완료",
    "node_hint": "트리에서 주문 클릭 · {key} 다음 주문",
    "node_set_target": "학습 대상 지정 {key}",
    "node_is_target": "현재 학습 대상",
    "node_locked": "선행 주문 필요",
    "node_mastered": "숙달 완료",
    "legend_mastered": "숙달",
    "legend_learning": "학습 중",
    "legend_can_learn": "배울 수 있음",
    "preview_title": "미리보기",
    "tree_title": "주문 트리",
    "progress_candidates": "배울 수 있는 주문 {n}개 · {key} 다음 주문",
    "progress_no_other": "배울 수 있는 다른 주문 없음",
    "xp": "{cur} / {need} XP",
    "tree_no_links": "연결된 주문 없음",
    "tree_exit": "+{n}",
    "node_can_learn": "배울 수 있음",
    "node_target_pct": "현재 학습 대상 · {pct}%",
    "node_learning_pct": "미리 배움 · 숙달 {pct}%",
    "node_ready": "경험치 충족 · 습득 대기",
    "node_unknown": "상태 확인 중",
    "node_sub": "{school} · {level}",
    "req_all": "선행 주문 모두 숙달 필요",
    "req_some": "선행 주문 중 {n}개 숙달 필요",
    "req_none": "선행 주문 없음",
    "req_mastered": "{name} · 숙달",
    "req_not_mastered": "{name} · 미숙달",
    "req_checking": "{name} · 확인 중",
    "req_not_in_game": "{name} · 게임에 없음",
    "req_more": "외 {n}개",
}
SCHOOL_TEXT = {"Alteration": "변화마법", "Conjuration": "소환마법", "Destruction": "파괴마법", "Illusion": "환영마법",
               "Restoration": "회복마법"}
# Widget.cpp:297 LevelName reads the game's GMSTs; the Korean words are the ones the list's 레벨 column shows for the
# same tree levels (소각 Expert=전문가, 불꽃의 룬 Apprentice=수습생, 치명적인 독 분사 Adept=숙련자, 화염 폭풍 Master=달인)
LEVEL_TEXT = {"Novice": "초보자", "Apprentice": "수습생", "Adept": "숙련자", "Expert": "전문가", "Master": "달인"}


def fill(str_id: str, /, **values: object) -> str:
    text = S[str_id]
    for name, value in values.items():
        text = text.replace("{" + name + "}", str(value))
    return text


# ------------------------------------------------------------------------------------------------------------
# Canvas: stage units -> pixels (stage x 0..1280, y -40..760 is the 1280x800 frame, LOG: "visible 0..1280 x -40..760")
# ------------------------------------------------------------------------------------------------------------

STAGE_Y0 = -40.0


def rgb_t(rgb: int) -> tuple[int, int, int]:
    return (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255


class Canvas:
    def __init__(self, image: Image.Image, ss: float) -> None:
        self.img = image
        self.ss = ss
        self.draw = ImageDraw.Draw(image)
        self.ox = 0.0
        self.oy = 0.0

    # an offset stack is enough: no clip in the widget is rotated or scaled
    def at(self, dx: float, dy: float) -> "Offset":
        return Offset(self, dx, dy)

    def p(self, x: float, y: float) -> tuple[float, float]:
        return (self.ox + x) * self.ss, (self.oy + y - STAGE_Y0) * self.ss

    def rect_fill(self, x: float, y: float, w: float, h: float, rgb: int, alpha: float) -> None:
        x0, y0 = self.p(x, y)
        x1, y1 = self.p(x + w, y + h)
        box = tuple(int(round(v)) for v in (x0, y0, x1, y1))
        if alpha >= 100:
            self.draw.rectangle((box[0], box[1], box[2] - 1, box[3] - 1), fill=rgb_t(rgb))
            return
        region = self.img.crop(box)
        solid = Image.new("RGB", region.size, rgb_t(rgb))
        self.img.paste(Image.blend(region, solid, alpha / 100.0), box[:2])

    def polyline(self, points: list[tuple[float, float]], width: float, rgb: int) -> None:
        px = [self.p(x, y) for x, y in points]
        self.draw.line(px, fill=rgb_t(rgb), width=max(1, int(round(width * self.ss))), joint="curve")

    def polygon(self, points: list[tuple[float, float]], rgb: int) -> None:
        self.draw.polygon([self.p(x, y) for x, y in points], fill=rgb_t(rgb))

    def text_line(self, x: float, baseline: float, text: str, style: Style, rgb: int,
                  clip: tuple[float, float, float, float] | None = None) -> None:
        stage_clip = None
        if clip:
            stage_clip = (self.ox + clip[0], self.oy + clip[1] - STAGE_Y0, self.ox + clip[2], self.oy + clip[3] - STAGE_Y0)
        te.draw_line(self.img, self.ox + x, self.oy + baseline - STAGE_Y0, text, style, rgb, self.ss, stage_clip)


class Offset:
    def __init__(self, canvas: Canvas, dx: float, dy: float) -> None:
        self.c, self.dx, self.dy = canvas, dx, dy

    def __enter__(self) -> Canvas:
        self.c.ox += self.dx
        self.c.oy += self.dy
        return self.c

    def __exit__(self, *_: object) -> None:
        self.c.ox -= self.dx
        self.c.oy -= self.dy


# ------------------------------------------------------------------------------------------------------------
# Text fields (Draw.h:61 Text, Widget.cpp:471-567 Size/LineH/FieldH/TextW/TextH/FitText/FitLine/FitLines)
# ------------------------------------------------------------------------------------------------------------

@dataclass
class Ctx:
    row_size: float  # the list row size Size() uses (Widget.cpp:471-474)

    def size(self, base: float) -> float:
        return max(base, self.row_size)


def line_h(size: float) -> float:
    return size * LINE_FACTOR


def field_h(size: float, lines: int = 1) -> float:
    return line_h(size) * lines + GUTTER


def cut(text: str, n: int) -> str:
    return text[:n] + "..." if len(text) > n else text  # Draw.h:84 Cut (code points)


def text_wh(text: str, size: float, w: float, wrap: bool) -> tuple[float, float]:
    tw, th, _ = te.text_size(text, Style(size), w, wrap)
    return tw + GUTTER, th + GUTTER


def fits(text: str, size: float, w: float, h: float, wrap: bool) -> bool:
    tw, th = text_wh(text, size, w, wrap)
    return tw <= w + 0.5 and th <= h + 0.5


def fit_text(w: float, h: float, part: str, compose, size: float, wrap: bool) -> str:
    """Widget.cpp:519 FitText: the whole text, else bisection on code points with "..."."""
    if fits(compose(part), size, w, h, wrap):
        return compose(part)
    lo, hi = 0, len(part)
    while lo < hi:
        mid = (lo + hi) // 2
        if fits(compose(cut(part, mid)), size, w, h, wrap):
            lo = mid + 1
        else:
            hi = mid
    best = lo - 1 if lo > 0 else 0
    return compose(cut(part, best))


def draw_field(c: Canvas, x: float, y: float, w: float, h: float, text: str, rgb: int, size: float,
               align: str = "left", wrap: bool = False) -> tuple[float, float]:
    """Draws an AS2 text field's text (clipped to the field) and returns (TextW, TextH)."""
    style = Style(size)
    tw, th, lines = te.text_size(text, style, w, wrap)
    inner_l, inner_r = x + te.GUTTER, x + w - te.GUTTER
    for i, line in enumerate(lines):
        lw = te.width(line, style)
        lx = inner_l if align == "left" else inner_r - lw if align == "right" else (inner_l + inner_r - lw) / 2
        baseline = y + te.GUTTER + te.BASELINE * size + i * te.LINE_PITCH * size
        c.text_line(lx, baseline, line, style, rgb, clip=(x, y, x + w, y + h))
    return tw + GUTTER, th + GUTTER


def fit_line(c: Canvas, x: float, y: float, w: float, size: float, text: str, rgb: int, align: str = "left") -> tuple[float, float]:
    h = field_h(size)
    final = fit_text(w, h, text, lambda p: p, size, False)
    return draw_field(c, x, y, w, h, final, rgb, size, align)


def fit_lines(c: Canvas, x: float, y: float, w: float, size: float, lines: int, text: str, rgb: int,
              align: str = "left") -> tuple[float, float]:
    h = field_h(size, lines)
    final = fit_text(w, h, text, lambda p: p, size, True)
    return draw_field(c, x, y, w, h, final, rgb, size, align, wrap=True)


# ------------------------------------------------------------------------------------------------------------
# Shapes (Draw.h:117-202, Widget.cpp:1551 DrawMark)
# ------------------------------------------------------------------------------------------------------------

def diamond_pts(cx: float, cy: float, r: float) -> list[tuple[float, float]]:
    return [(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy), (cx, cy - r)]


def arc_pts(cx: float, cy: float, r: float, fraction: float) -> list[tuple[float, float]]:
    fraction = min(max(fraction, 0.0), 1.0)
    steps = max(1, math.ceil(32 * fraction))
    pts = [(cx, cy - r)]
    for i in range(1, steps + 1):
        t = -math.pi / 2 + 2 * math.pi * fraction * i / steps
        pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
    return pts


def draw_mark(c: Canvas, x: float, y: float, r: float, state: State, percent: float) -> None:
    if state == State.MASTERED:
        c.polygon(diamond_pts(x, y, r), GOLD)
        c.polyline(diamond_pts(x, y, r), LINE_W, GOLD)
    elif state in (State.TARGET, State.LEARNING, State.READY):
        c.polyline(diamond_pts(x, y, r), LINE_W, WHITE)
        c.polyline(arc_pts(x, y, r + RING_GAP, 1.0 if state == State.READY else percent / 100.0), ARC_W, BLUE)
    elif state == State.AVAILABLE:
        c.polyline(diamond_pts(x, y, r), LINE_W, WHITE)
    else:  # Draw.h:155 Padlock, a_size = 2r
        size = 2 * r
        bw, bh, sr = 0.8 * size, 0.5 * size, 0.25 * size
        top = y + size / 2 - bh
        c.rect_fill(x - bw / 2, top, bw, bh, PADLOCK, 100)
        c.polyline([(x + sr * math.cos(math.pi + math.pi * i / 8), top + sr * math.sin(math.pi + math.pi * i / 8)) for i in range(9)],
                   max(1.0, size / 8), PADLOCK)


def draw_check(c: Canvas, x: float, y: float, size: float, rgb: int) -> None:
    c.polyline([(x + size * 0.1, y + size * 0.55), (x + size * 0.4, y + size * 0.85), (x + size * 0.9, y + size * 0.15)],
               max(MARK_MIN_W, size / MARK_W_SHARE), rgb)


def draw_cross(c: Canvas, x: float, y: float, size: float, rgb: int) -> None:
    w = max(MARK_MIN_W, size / MARK_W_SHARE)
    c.polyline([(x + size * 0.15, y + size * 0.15), (x + size * 0.85, y + size * 0.85)], w, rgb)
    c.polyline([(x + size * 0.85, y + size * 0.15), (x + size * 0.15, y + size * 0.85)], w, rgb)


PANEL_BORDER = True  # build 5; layouts.py sets False for build 6


def panel(c: Canvas, rect: tuple[float, float, float, float], title: str | None, alpha: float) -> None:
    """Widget.cpp:1624 Panel + Draw.h:111 Box."""
    l, t, r, b = rect
    c.rect_fill(l, t, r - l, b - t, PANEL_FILL, alpha)
    if PANEL_BORDER:  # build 6 draws no border (Widget.cpp Panel: "no border", user 2026-10-25)
        c.polyline([(l, t), (r, t), (r, b), (l, b), (l, t)], EDGE_W, PANEL_EDGE)
    if title:
        with c.at(l, t):
            draw_field(c, PAD, PAD / 2, (r - l) - 2 * PAD, TITLE_H, title, TITLE_COLOR, TITLE_SIZE)


# ------------------------------------------------------------------------------------------------------------
# Scenario state
# ------------------------------------------------------------------------------------------------------------

@dataclass
class Scenario:
    name: str
    frame: Path
    school: str
    list_spell: str              # the list's selected spell (tree name)
    clicked: str | None          # a tree click (node card + centre), None = nothing clicked
    targets: dict[str, tuple[str, float]] = field(default_factory=dict)  # school -> (spell name, percent)
    effect_tint_green: bool = False
    card: str = "original"       # "original" (crop + 레벨 fix) or "rejuvenating" (P2 card for 회춘의 손길)
    notes: list[str] = field(default_factory=list)


@dataclass
class World:
    tree: mm.Tree
    known: list[tuple[str, int]]  # (name, saved FormID) of the spells the player knows (ASSUMED from the list)
    required_xp: dict[str, float]

    def known_ids(self) -> set[int]:
        return {form_id for _, form_id in self.known}


def node_index(tree: mm.Tree, name: str, school: str) -> int:
    for i in mm.nodes_named(tree, name):
        if tree.nodes[i].school == school and not mm.is_dup(tree.nodes[i]):
            return i
    raise ValueError(f"no non-dup node named {name!r} in {school}")


# ------------------------------------------------------------------------------------------------------------
# Widget drawing (Widget.cpp RedrawTree / RedrawProgress / RedrawNodeCard / DrawLegend)
# ------------------------------------------------------------------------------------------------------------

def tree_content():
    """Settings.h:67 TreeContent."""
    w, h = TREE[2] - TREE[0], TREE[3] - TREE[1]
    body = h - PANEL_TITLE_H - TREE_LEGEND_H
    return w / 2, PANEL_TITLE_H + body / 2, w / 2 - PANEL_PAD, body / 2 - PANEL_PAD


def draw_legend(c: Canvas, ctx: Ctx) -> None:
    items = [(State.MASTERED, 100.0, S["legend_mastered"]), (State.TARGET, 70.0, S["legend_learning"]),
             (State.AVAILABLE, 0.0, S["legend_can_learn"])]
    w, h = TREE[2] - TREE[0], TREE[3] - TREE[1]
    y = h - TREE_LEGEND_H / 2 - PAD / 2
    room, icon = w - 2 * PAD, 2 * (LEGEND_ICON_R + RING_GAP)
    size = ctx.size(LEGEND_SIZE)
    while True:
        fh = field_h(size)
        widths = [text_wh(t, size, room, False)[0] for _, _, t in items]
        used = sum(icon + LEGEND_TEXT_GAP + tw for tw in widths)
        if used > room and size - 1 >= LEGEND_SIZE:
            size -= 1
            continue
        gap = max(0.0, (room - used) / len(items))
        x = PAD + gap / 2
        with c.at(TREE[0], TREE[1]):
            for (state, pct, text), tw in zip(items, widths):
                draw_mark(c, x + LEGEND_ICON_R + RING_GAP, y, LEGEND_ICON_R, state, pct)
                x += icon + LEGEND_TEXT_GAP
                draw_field(c, x, y - fh / 2, room, fh, text, DIM_TEXT, size)
                x += tw + gap
        log.info("legend at size %.1f: %.0f of %.0f units", size, used, room)
        return


@dataclass
class TreeResult:
    shown: list[int]
    centre: int
    lines: list[str]


def redraw_tree(c: Canvas, ctx: Ctx, tree: mm.Tree, snap: mm.Snapshot, centre: int, selected: int) -> TreeResult:
    cx, cy, half_w, half_h = tree_content()
    school = mm.school_named(tree, tree.nodes[centre].school)
    rot = mm.school_rotation_deg(tree, school) if school else 0.0
    nodes, edges = mm.query(tree, centre, rot, TREE_SCALE, half_w, half_h, MAX_STEPS, MAX_NODES)
    looks = {p.index: mm.look_of(tree, snap, p.index) for p in nodes}
    shown = [p for p in nodes if p.index == centre or looks[p.index][0] in mm.SHOWN_STATES]
    shown_set = {p.index for p in shown}
    if selected >= 0 and selected not in shown_set:
        selected = -1
    out = [f"centre {tree.nodes[centre].name} rotation {rot:.0f}, linked {len(nodes)} -> drawn {len(shown)}"]
    for p in nodes:
        st = looks[p.index][0].value
        out.append(f"  {tree.nodes[p.index].name}: {st} at ({p.sx:.1f}, {p.sy:.1f}){' DRAWN' if p.index in shown_set else ''}")
    with c.at(TREE[0] + cx, TREE[1] + cy):
        for pe in edges:  # Widget.cpp:1685 DrawEdges
            edge = tree.edges[pe.edge]
            if not pe.a_shown or not pe.b_shown or edge.a not in shown_set or edge.b not in shown_set:
                continue
            if edge.lock:
                length = math.hypot(pe.bx - pe.ax, pe.by - pe.ay)
                ux, uy = (pe.bx - pe.ax) / length, (pe.by - pe.ay) / length
                s = 0.0
                while s < length:
                    e = min(s + DASH, length)
                    c.polyline([(pe.ax + ux * s, pe.ay + uy * s), (pe.ax + ux * e, pe.ay + uy * e)], LINE_W, LOCK_EDGE_COLOR)
                    s += DASH + DASH_GAP
            else:
                c.polyline([(pe.ax, pe.ay), (pe.bx, pe.by)], LINE_W, EDGE_COLOR)
        order = sorted(shown, key=lambda p: 0 if p.index == centre else 1 if p.index == selected else 2)
        reach = lambda i: SELECT_R if i == selected else HIT_R  # noqa: E731  Widget.cpp:1704 NodeReach
        boxes = [(p.sx - reach(p.index), p.sy - reach(p.index), p.sx + reach(p.index), p.sy + reach(p.index)) for p in order]
        node_boxes = len(boxes)
        for p in order:
            state, pct = looks[p.index]
            with c.at(p.sx, p.sy):
                if p.index == selected:  # Widget.cpp:1749 DrawSelection
                    for sx_ in (-1, 1):
                        for sy_ in (-1, 1):
                            c.polyline([(sx_ * SELECT_R, sy_ * (SELECT_R - SELECT_ARM)), (sx_ * SELECT_R, sy_ * SELECT_R),
                                        (sx_ * (SELECT_R - SELECT_ARM), sy_ * SELECT_R)], LINE_W, WHITE)
                draw_mark(c, 0, 0, NODE_R, state, pct)
                keep = p.index in (centre, selected)
                draw_label(c, ctx, tree, p, state, pct, keep, half_h, reach(p.index), boxes, out)
        focus = selected if selected >= 0 else centre
        exits = find_exits(tree, snap, focus, shown_set, rot)
        no_links = len(shown) == 1 and not tree.adjacent[focus]
        if sum(exits) > 0:
            draw_exits(c, ctx, exits, half_w, half_h)
            out.append(f"exits of {tree.nodes[focus].name}: {exits}")
        elif no_links:
            size = ctx.size(NOTE_SIZE)
            fit_line(c, -half_w, half_h - field_h(size), 2 * half_w, size, S["tree_no_links"], DIM_TEXT, "center")
            out.append("no links note")
        else:
            out.append(f"exits of {tree.nodes[focus].name}: none")
        out.append(f"labels drawn {len(boxes) - node_boxes}")
    return TreeResult([p.index for p in shown], centre, out)


LABEL_SIDE: dict[int, bool] = {}  # node index -> label on the left; set by layouts.py for build 6's centre label rule


def draw_label(c: Canvas, ctx: Ctx, tree: mm.Tree, p: mm.Placed, state: State, pct: float, keep: bool, half_h: float,
               reach: float, drawn: list, out: list[str]) -> None:
    """Widget.cpp:1721 DrawLabel (c is at the node)."""
    percent = state in (State.TARGET, State.LEARNING)
    suffix = f"\r{pct:.0f}%" if percent else ""
    size = ctx.size(LABEL_SIZE)
    box_h = field_h(size, LABEL_LINES + (1 if percent else 0))
    left = LABEL_SIDE.get(p.index, p.sx > 0)
    gap = reach + LABEL_GAP
    name = tree.nodes[p.index].name
    final = fit_text(LABEL_W, box_h, name, lambda n: n + suffix, size, True)
    tw, th = text_wh(final, size, LABEL_W, True)
    width, height = min(tw, LABEL_W), min(th, box_h)
    limit = half_h + PAD
    top = min(max(p.sy - height / 2, -limit), max(-limit, limit - height)) - p.sy
    fx = -gap - LABEL_W if left else gap
    frm = p.sx - gap - width if left else p.sx + gap
    box = (frm, p.sy + top, frm + width, p.sy + top + height)
    overlap = any(box[0] < o[2] and o[0] < box[2] and box[1] < o[3] and o[1] < box[3] for o in drawn)
    if not keep and overlap:
        out.append(f"  label dropped (overlap): {name}")
        return
    drawn.append(box)
    draw_field(c, fx, top, LABEL_W, box_h, final, LABEL_COLOR, size, "right" if left else "left", wrap=True)


def find_exits(tree: mm.Tree, snap: mm.Snapshot, focus: int, shown: set[int], rot: float) -> list[int]:
    """Widget.cpp:1811 FindExits: 8 sectors of linked, drawable, not drawn nodes."""
    sectors = [0] * 8
    f = tree.nodes[focus]
    rad = math.radians(rot)
    c_, s_ = math.cos(rad), math.sin(rad)
    seen: set[int] = set()
    for e in tree.adjacent[focus]:
        edge = tree.edges[e]
        other = edge.b if edge.a == focus else edge.a
        if other == focus or other in shown or other in seen:
            continue
        seen.add(other)
        if mm.look_of(tree, snap, other)[0] not in mm.SHOWN_STATES:
            continue
        n = tree.nodes[other]
        if not n.has_xy or not f.has_xy:
            continue
        dx, dy = n.x - f.x, n.y - f.y
        angle = math.atan2(dx * s_ + dy * c_, dx * c_ - dy * s_)
        sectors[(round(angle / (2 * math.pi / 8)) % 8 + 8) % 8] += 1
    return sectors


def draw_exits(c: Canvas, ctx: Ctx, sectors: list[int], half_w: float, half_h: float) -> None:
    """Widget.cpp:1843 DrawExits."""
    size = ctx.size(NOTE_SIZE)
    h = field_h(size)
    hw, hh = half_w - EXIT_INSET, half_h - EXIT_INSET
    for i, n in enumerate(sectors):
        if not n:
            continue
        angle = i * 2 * math.pi / 8
        ux, uy = math.cos(angle), math.sin(angle)
        t = min(hw / abs(ux) if abs(ux) > 1e-9 else 1e9, hh / abs(uy) if abs(uy) > 1e-9 else 1e9)
        px, py, a = ux * t, uy * t, EXIT_ARROW
        c.polygon([(px + ux * a, py + uy * a), (px - ux * a - uy * a, py - uy * a + ux * a), (px - ux * a + uy * a, py - uy * a - ux * a)], LABEL_COLOR)
        cx = min(max(px - ux * (a + EXIT_TEXT_W / 2), -half_w + EXIT_TEXT_W / 2), half_w - EXIT_TEXT_W / 2)
        cy = min(max(py - uy * (a + h / 2), -half_h - PAD + h / 2), half_h + PAD - h / 2)
        fit_line(c, cx - EXIT_TEXT_W / 2, cy - h / 2, EXIT_TEXT_W, size, fill("tree_exit", n=n), LABEL_COLOR, "center")


def tally(tree: mm.Tree, snap: mm.Snapshot, school: str) -> dict[str, int]:
    """Widget.cpp:609 SchoolInfoOf."""
    learn = mm.learnable(tree, snap, school)
    t = {"candidates": sum(1 for i in learn if tree.nodes[i].has_xy), "available": len(learn), "target": 0, "ready": 0,
         "learning": 0, "mastered": 0, "locked": 0, "unknown": 0}
    for i in mm.school_named(tree, school).nodes:
        if mm.is_dup(tree.nodes[i]):
            continue
        st = mm.look_of(tree, snap, i)[0]
        if st == State.AVAILABLE:
            continue
        t[{State.TARGET: "target", State.READY: "ready", State.LEARNING: "learning", State.MASTERED: "mastered",
           State.LOCKED: "locked"}.get(st, "unknown")] += 1
    return t


def next_step_line(t: dict[str, int], has_target: bool) -> str:
    """Widget.cpp:1954 NextStepLine (the branches the scenarios can reach, then the rest in order)."""
    if t["candidates"] > 0:
        return fill("progress_candidates" if has_target else "progress_none_candidates", n=t["candidates"], key=KEY_NEXT)
    if t["locked"] > 0:
        return S["progress_none_nothing"]
    if has_target:
        return S["progress_no_other"]
    return S["progress_all_mastered"]


def redraw_progress(c: Canvas, ctx: Ctx, tree: mm.Tree, snap: mm.Snapshot, school: str, out: list[str],
                    top: float = CARD_TOP) -> None:
    """`top` other than CARD_TOP only for layout proposals (layouts.py E2)."""
    w = PROGRESS[2] - PROGRESS[0] - 2 * PAD
    with c.at(PROGRESS[0] + PAD, PROGRESS[1]):
        y = top
        title_size, hint_size = ctx.size(SCHOOL_SIZE), ctx.size(HINT_SIZE)
        fit_line(c, 0, y, w, title_size, fill("progress_title", school=SCHOOL_TEXT[school]), TITLE_COLOR)
        y += field_h(title_size)
        t = tally(tree, snap, school)
        out.append(f"tally {school}: {t}")
        target, pct, required = snap.schools[school]
        if not target:
            line = next_step_line(t, False)
            fit_lines(c, 0, y, w, hint_size, MESSAGE_LINES, line, WHITE)
            out.append(f"progress message: {line}")
            return
        name = tree.nodes[mm.find(tree, target, school)].name
        pct_s = f"{pct:.0f}"
        target_h = field_h(TARGET_SIZE)
        final = fit_text(w, target_h, name, lambda n: fill("progress_target", spell=n, pct=pct_s), TARGET_SIZE, False)
        draw_field(c, 0, y, w, target_h, final, WHITE, TARGET_SIZE)
        y += target_h + BAR_GAP
        xp_size = ctx.size(XP_SIZE)
        xp_h = field_h(xp_size)
        bar_w = max(0.0, w - XP_W - PAD)
        c.rect_fill(0, y, bar_w, BAR_H, BAR_BACK, 100)
        if pct > 0:
            c.rect_fill(0, y, bar_w * min(max(pct / 100.0, 0.0), 1.0), BAR_H, BLUE, 100)
        xp = fill("xp", cur=f"{required * pct / 100:.0f}", need=f"{required:.0f}")
        fit_line(c, bar_w + PAD, y + BAR_H / 2 - xp_h / 2, XP_W, xp_size, xp, WHITE, "right")
        y += BAR_H + HINT_GAP
        nxt = next_step_line(t, True)
        fit_line(c, 0, y, w, hint_size, nxt, LABEL_COLOR)
        out.append(f"progress: {final} | {xp} | {nxt}")


def state_line(state: State, pct: float, button: bool) -> str:
    """Widget.cpp:2037 StateLine."""
    p = f"{pct:.0f}"
    return {
        State.TARGET: S["node_is_target"] if button else fill("node_target_pct", pct=p),
        State.LEARNING: fill("node_learning_pct", pct=p),
        State.AVAILABLE: S["node_can_learn"],
        State.READY: S["node_ready"],
        State.MASTERED: S["node_mastered"],
        State.LOCKED: S["node_locked"],
    }.get(state, S["node_unknown"])


def requirement_rows(tree: mm.Tree, snap: mm.Snapshot, node: mm.Node) -> list[tuple[str, str | None, int, str]]:
    """Widget.cpp:2070 RequirementRows: (name, format key, colour, mark)."""
    rows = []

    def add(saved: int) -> None:
        index = mm.find(tree, saved, node.school)
        name = tree.nodes[index].name if index >= 0 else f"{saved:08X}"
        pr = mm.prereq_of(tree, snap, saved, node.school)
        rows.append({mm.Prereq.MASTERED: (name, "req_mastered", OK, "check"),
                     mm.Prereq.NOT_MASTERED: (name, "req_not_mastered", NOT_YET, "cross"),
                     mm.Prereq.UNKNOWN: (name, "req_checking", DIM_TEXT, "space"),
                     mm.Prereq.NOT_IN_GAME: (name, "req_not_in_game", DIM_TEXT, "space")}[pr])

    if node.hard:
        rows.append((S["req_all"], None, TITLE_COLOR, "none"))
        for s_ in node.hard:
            add(s_)
    if node.soft and node.soft_needed > 0:
        rows.append((fill("req_some", n=node.soft_needed), None, TITLE_COLOR, "none"))
        for s_ in node.soft:
            add(s_)
    if not rows:
        rows.append((S["req_none"], None, TITLE_COLOR, "none"))
    return rows


def draw_rows(c: Canvas, ctx: Ctx, rows: list, w: float, top: float, bottom: float, out: list[str]) -> None:
    """Widget.cpp:2097 DrawRows."""
    size = ctx.size(ROW_SIZE)
    h, pitch = field_h(size), line_h(size) + GUTTER / 2
    fit_n = max(0, math.floor((bottom - top - (h - pitch)) / pitch))
    if len(rows) > fit_n > 0:
        hidden = len(rows) - (fit_n - 1)
        rows = rows[: fit_n - 1] + [(fill("req_more", n=hidden), None, DIM_TEXT, "none")]
    mark = size * MARK_SHARE
    text_x = mark + MARK_GAP
    for i, (name, fmt, rgb, kind) in enumerate(rows[:fit_n]):
        y = top + pitch * i
        x = 0.0 if kind == "none" else text_x
        my = y + h / 2 - mark / 2
        if kind == "check":
            draw_check(c, 0, my, mark, rgb)
        if kind == "cross":
            draw_cross(c, 0, my, mark, rgb)
        compose = (lambda n, f=fmt: fill(f, name=n)) if fmt else (lambda n: n)
        final = fit_text(w - x, h, name, compose, size, False)
        draw_field(c, x, y, w - x, h, final, rgb, size)
        out.append(f"row {i}: [{kind}] {final}")


def redraw_node_card(c: Canvas, ctx: Ctx, tree: mm.Tree, snap: mm.Snapshot, selected: int, out: list[str]) -> None:
    w, h = NODE_CARD[2] - NODE_CARD[0] - 2 * PAD, NODE_CARD[3] - NODE_CARD[1]
    with c.at(NODE_CARD[0] + PAD, NODE_CARD[1]):
        if selected < 0:
            text = fill("node_hint", key=KEY_NEXT)
            fit_lines(c, 0, CARD_TOP, w, ctx.size(ROW_SIZE), HINT_LINES, text, DIM_TEXT)
            out.append(f"node card hint: {text}")
            return
        node = tree.nodes[selected]
        state, pct = mm.look_of(tree, snap, selected)
        y = CARD_TOP
        _, name_h = fit_lines(c, 0, y, w, NAME_SIZE, NAME_LINES, node.name, WHITE)
        y += min(max(name_h, field_h(NAME_SIZE)), field_h(NAME_SIZE, NAME_LINES))
        sub_size = ctx.size(SUB_SIZE)
        level = LEVEL_TEXT.get(node.skill_level, node.skill_level)
        sub = fill("node_sub", school=SCHOOL_TEXT[node.school], level=level) if level else SCHOOL_TEXT[node.school]
        fit_line(c, 0, y, w, sub_size, sub, TITLE_COLOR)
        y += field_h(sub_size)
        state_size = ctx.size(STATE_SIZE)
        state_h = field_h(state_size)
        draw_mark(c, STATE_ICON_R + RING_GAP, y + state_h / 2, STATE_ICON_R, state, pct)
        st = state_line(state, pct, False)
        fit_line(c, STATE_TEXT_X, y, w - STATE_TEXT_X, state_size, st, WHITE)
        y += state_h + RULE_GAP
        c.polyline([(0, y), (w, y)], EDGE_W, PANEL_EDGE)
        bottom_rule = h - BOTTOM_RULE_FROM_BOTTOM
        draw_rows(c, ctx, requirement_rows(tree, snap, node), w, y + RULE_GAP, bottom_rule - RULE_GAP, out)
        c.polyline([(0, bottom_rule), (w, bottom_rule)], EDGE_W, PANEL_EDGE)
        spell = snap.spells.get(node.form_id)
        required = spell.required if spell else 0.0
        xp = fill("xp", cur=f"{required * pct / 100:.0f}", need=f"{required:.0f}")
        fit_line(c, 0, h - XP_FROM_BOTTOM, w, state_size, xp, LABEL_COLOR, "right")
        # Widget.cpp:2125 DrawButton
        enabled = state == State.AVAILABLE
        with c.at(0, h - BUTTON_FROM_BOTTOM):
            c.rect_fill(0, 0, w, BUTTON_H, BUTTON_FILL if enabled else BUTTON_OFF, 100)
            c.polyline([(0, 0), (w, 0), (w, BUTTON_H), (0, BUTTON_H), (0, 0)], EDGE_W, WHITE if enabled else LOCKED_EDGE)
            label = fill("node_set_target", key=KEY_SET_TARGET) if enabled else state_line(state, pct, True)
            bsize = ctx.size(BUTTON_SIZE)
            fit_line(c, 0, (BUTTON_H - field_h(bsize)) / 2, w, bsize, label, WHITE if enabled else DIM_TEXT, "center")
        out.append(f"node card: {node.name} | {sub} | {st} | {xp} | button '{label}'")


# ------------------------------------------------------------------------------------------------------------
# Real-frame parts: background, 3D effect crop, SkyUI item card
# ------------------------------------------------------------------------------------------------------------

RIGHT_FROM = 588  # the SkyUI list panel's right edge in the 1280x800 frame; everything right of it is repainted
OLD_PANELS = [(645, 104, 960, 374), (968, 104, 1241, 374), (645, 376, 972, 485), (976, 376, 1241, 667), (684, 503, 1115, 763)]
DARKEN = 0.85
BLUR = 12


def load_frame(path: Path) -> Image.Image:
    img = Image.open(path).convert("RGB")
    if img.size != (1280, 800):
        img = img.resize((1280, 800), Image.LANCZOS)  # the 2 fps clip frames are 1152x720 = 0.9 of the stage frame
    return img


def repaint_right(frame: Image.Image) -> Image.Image:
    """Old panels out (normalised Gaussian fill from their surroundings), then blur and darken the right part."""
    a = np.asarray(frame).astype(np.float64)
    keep = np.ones(a.shape[:2])
    for x0, y0, x1, y1 in OLD_PANELS:
        keep[y0:y1, x0:x1] = 0
    filled = a.copy()
    # widest first; a narrower pass replaces the estimate only where it has enough known pixels around (a large hole's
    # middle keeps the wide estimate instead of dividing by almost nothing)
    for i, sigma in enumerate((90, 45, 20)):
        wimg = Image.fromarray((keep * 255).astype(np.uint8))
        wb = np.asarray(wimg.filter(ImageFilter.GaussianBlur(sigma))).astype(np.float64) / 255
        num = np.stack([np.asarray(Image.fromarray((a[..., k] * keep).clip(0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(sigma))).astype(np.float64) for k in range(3)], -1)
        est = num / np.maximum(wb[..., None], 1e-4)
        usable = (wb > (0.0 if i == 0 else 0.15))[..., None]
        filled = np.where(keep[..., None] > 0, a, np.where(usable, est, filled))
    out = Image.fromarray(filled.clip(0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(BLUR))
    o = np.asarray(out).astype(np.float64)
    ramp = np.clip((np.arange(1280) - RIGHT_FROM) / 40.0, 0, 1)  # soft step into the darkening
    o = o * (1 - (1 - DARKEN) * ramp)[None, :, None]
    result = a.copy()
    result[:, RIGHT_FROM:] = o[:, RIGHT_FROM:]
    return Image.fromarray(result.clip(0, 255).astype(np.uint8))


EFFECT_SRC = (650, 109, 956, 369)  # inside the old PREVIEW box of the 1280x800 frame
EFFECT_TITLE = (650, 109, 722, 131)  # the old "PREVIEW" title text, blanked
EFFECT_FEATHER = 14.0  # px faded at the source box edges, where the old box cut the halo


def effect_crop(heal_frame: Image.Image, green: bool) -> Image.Image:
    """The healing orb (MAGINVHealSpellArt) from a build-3 frame. Its extent is measured with a low threshold
    (the halo is part of the model's bound) and made symmetric around the bright core, standing in for the model's
    bound centre that Preview centres; the dim halo is pushed toward black (build 5 draws the model over an
    opaque black box) and faded at the old box's edges."""
    a = np.asarray(heal_frame.crop(EFFECT_SRC)).astype(np.float64)
    tx0, ty0, tx1, ty1 = (v - o for v, o in zip(EFFECT_TITLE, EFFECT_SRC[:2] * 2))
    a[ty0:ty1, tx0:tx1] = 0
    h, w = a.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    edge = np.minimum.reduce([xx, w - 1 - xx, h - 1 - yy, yy])
    a *= np.clip(edge / EFFECT_FEATHER, 0, 1)[..., None]
    lum = a.mean(axis=2)
    a *= np.clip((lum - 22) / 45.0, 0, 1)[..., None]
    lum = a.mean(axis=2)
    core = lum > 200
    cy, cx = (np.nonzero(core)[0].mean(), np.nonzero(core)[1].mean()) if core.any() else (h / 2, w / 2)
    ys, xs = np.nonzero(lum > 6)
    hx = max(cx - xs.min(), xs.max() - cx) + 1
    hy = max(cy - ys.min(), ys.max() - cy) + 1
    out = np.zeros((int(2 * hy), int(2 * hx), 3))
    oy, ox = int(hy - cy), int(hx - cx)
    sy0, sx0 = max(0, -oy), max(0, -ox)
    sy1, sx1 = min(h, out.shape[0] - oy), min(w, out.shape[1] - ox)
    out[sy0 + oy:sy1 + oy, sx0 + ox:sx1 + ox] = a[sy0:sy1, sx0:sx1]
    a = out
    if green:
        flat = a.reshape(-1, 3) / 255
        hsv = np.array([colorsys.rgb_to_hsv(*p) for p in flat])
        hsv[:, 0] = 0.30
        hsv[:, 1] = hsv[:, 1] * 0.8
        a = (np.array([colorsys.hsv_to_rgb(*p) for p in hsv]) * 255).reshape(a.shape)
    return Image.fromarray(a.clip(0, 255).astype(np.uint8))


def place_effect(canvas: Image.Image, ss: float, crop: Image.Image) -> str:
    """Fill PREVIEW_FILL of the content area (Settings.h:76 PanelContent), centred, clipped to it (scissor)."""
    l, t, r, b = PREVIEW[0] + PAD, PREVIEW[1] + TITLE_H + PAD, PREVIEW[2] - PAD, PREVIEW[3] - PAD
    scale = min(PREVIEW_FILL * (r - l) / crop.width, PREVIEW_FILL * (b - t) / crop.height)
    w, h = crop.width * scale, crop.height * scale
    cx, cy = (l + r) / 2, (t + b) / 2
    img = crop.resize((max(1, round(w * ss)), max(1, round(h * ss))), Image.LANCZOS)
    px, py = round((cx - w / 2) * ss), round((cy - h / 2 - STAGE_Y0) * ss)
    cl, ct, cr, cb = round(l * ss), round((t - STAGE_Y0) * ss), round(r * ss), round((b - STAGE_Y0) * ss)
    sx0, sy0 = max(0, cl - px), max(0, ct - py)
    sx1, sy1 = min(img.width, cr - px), min(img.height, cb - py)
    region = img.crop((sx0, sy0, sx1, sy1))
    base = canvas.crop((px + sx0, py + sy0, px + sx1, py + sy1))
    # additive over the opaque black box: black stays black, the orb glows
    merged = Image.fromarray(np.minimum(255, np.asarray(base).astype(int) + np.asarray(region).astype(int)).astype(np.uint8))
    canvas.paste(merged, (px + sx0, py + sy0))
    return f"effect {crop.width}x{crop.height} px -> {w:.1f}x{h:.1f} units (scale {scale:.3f}) centred at {cx:.1f},{cy:.1f}"


# The real card in base_v3_poison.png (1280x800): its black background and the text rows measured on it
CARD_SRC = (687, 506, 1111, 760)
CARD_CLEAN_FROM_X = 975  # right of this the old node card panel lies over the card (its border and hint text)
CARD_TITLE = ("치명적인 독 분사", (765, 525, 852, 537))
CARD_DESC1 = ("초당 20의 독 피해를 줍니다. 중독된 대상은 시간이 지남에 따라", (699, 550, 917, 558))
CARD_DESC_TOPS = (550, 565, 580)
CARD_DESC_WRAP = 225.0  # the real card: line 1 (219.8 wide here) fits, line 2 + " 목표는" (230.3) does not
CARD_LEVEL_LABEL = ("레", (697, 632, 702, 637))
CARD_LEVEL_VALUE = ("숙련자", (707, 629, 742, 641))
CARD_COST_LABEL = ("매지카", (857, 632, 874, 637))
CARD_COST_VALUE = ("52", (880, 631, 894, 641))
CARD_GREY, CARD_WHITE = 0x999999, 0xFFFFFF
# Log 2026-10-25 11:56:13 (build 4 game run): "SelfTest: item card PASS - background in _root 673.5,450.0..943.4,623.0"
CARD_STAGE_LEFT, CARD_STAGE_TOP, CARD_STAGE_W = 673.5, 450.0, 943.4 - 673.5


def card_style(spec):
    text, (x0, y0, x1, y1) = spec
    return te.fit_style(text, x1 - x0, y1 - y0, bold=True)


def card_draw(c: Canvas, text: str, style: Style, rgb: int, ink_top: float, x: float, align: str) -> None:
    ix0, iy0, ix1, _ = te.ink_box(text, style)
    w = ix1 - ix0
    left = x - ix0 if align == "left" else x - ix1 if align == "right" else x - (ix0 + ix1) / 2
    c.text_line(left, ink_top - iy0, text, style, rgb)


def build_card(base: Image.Image, kind: str, ss: float, spell: dict) -> Image.Image:
    """The SkyUI card at its frame size x ss. 'original': the real crop with SkillLevelLabel fixed (Widget.cpp:929
    FixLevelLabel: wordWrap off, autoSize right -> "레벨" whole, right edge kept); 'rejuvenating': the P2 card
    (Widget.cpp:1243 BuiltCardInfo) redrawn in the real card's measured text styles."""
    x0, y0, x1, y1 = CARD_SRC
    crop = base.crop(CARD_SRC)
    a = np.asarray(crop).copy()
    a[:, CARD_CLEAN_FROM_X - x0:] = 0
    if kind != "original":
        a[:] = 0
    card = Image.fromarray(a).resize((round((x1 - x0) * ss), round((y1 - y0) * ss)), Image.LANCZOS)
    c = Canvas(card, ss)
    c.oy = STAGE_Y0  # card units: no stage offset
    c.ox = 0.0
    lab = card_style(CARD_COST_LABEL)
    val = card_style(CARD_LEVEL_VALUE)
    num = card_style(CARD_COST_VALUE)
    lab_top = CARD_COST_LABEL[1][1] - y0
    level_label_right = CARD_LEVEL_LABEL[1][2] - x0 + 1
    if kind == "original":
        ImageDraw.Draw(card).rectangle(((CARD_LEVEL_LABEL[1][0] - x0 - 4) * ss, (CARD_LEVEL_LABEL[1][1] - y0 - 2) * ss,
                                        level_label_right * ss, (CARD_LEVEL_LABEL[1][3] - y0 + 3) * ss), fill=(0, 0, 0))
        card_draw(c, "레벨", lab, CARD_GREY, lab_top, level_label_right, "right")
        return card
    title = card_style(CARD_TITLE)
    desc = card_style(CARD_DESC1)
    centre = (CARD_TITLE[1][0] + CARD_TITLE[1][2]) / 2 - x0
    card_draw(c, spell["name"], title, CARD_WHITE, CARD_TITLE[1][1] - y0, centre, "center")
    mag = spell["magnitude"]
    lines = te.wrap(spell["description"], desc, CARD_DESC_WRAP)
    desc_centre = (CARD_DESC1[1][0] + CARD_DESC1[1][2]) / 2 - x0
    for i, line in enumerate(lines[: len(CARD_DESC_TOPS)]):
        top = CARD_DESC_TOPS[i] - y0
        card_draw(c, line, desc, CARD_GREY, top, desc_centre, "center")
        at = line.find(mag)
        while at >= 0:  # every magnitude in white, as the real card shows "20"
            ix0, iy0, ix1, _ = te.ink_box(line, desc)
            start = desc_centre - (ix0 + ix1) / 2 + te.width(line[:at], desc)
            c.text_line(start, top - iy0, mag, desc, CARD_WHITE)
            at = line.find(mag, at + len(mag))
    card_draw(c, "레벨", lab, CARD_GREY, lab_top, level_label_right, "right")
    card_draw(c, spell["level"], val, CARD_WHITE, CARD_LEVEL_VALUE[1][1] - y0, CARD_LEVEL_VALUE[1][0] - x0, "left")
    card_draw(c, "매지카", lab, CARD_GREY, lab_top, CARD_COST_LABEL[1][2] - x0 + 1, "right")
    card_draw(c, spell["cost"], num, CARD_WHITE, CARD_COST_VALUE[1][1] - y0, CARD_COST_VALUE[1][0] - x0, "left")
    return card


def place_card(canvas: Image.Image, ss_out: float, card: Image.Image, card_ss: float) -> str:
    scale = CARD_STAGE_W / (CARD_SRC[2] - CARD_SRC[0])
    w = card.width / card_ss * scale
    h = card.height / card_ss * scale
    img = card.resize((round(w * ss_out), round(h * ss_out)), Image.LANCZOS)
    canvas.paste(img, (round(CARD_STAGE_LEFT * ss_out), round((CARD_STAGE_TOP - STAGE_Y0) * ss_out)))
    return f"item card {CARD_SRC[2] - CARD_SRC[0]}x{CARD_SRC[3] - CARD_SRC[1]} frame px x {scale:.4f} -> {w:.1f}x{h:.1f} at {CARD_STAGE_LEFT},{CARD_STAGE_TOP}"


def annotate(canvas: Image.Image, ss: float, lines: list[str]) -> None:
    """A render note below the panels, outside everything the plugin draws (not part of the game picture)."""
    if not lines:
        return
    c = Canvas(canvas, ss)
    top = 672.0
    c.rect_fill(600, top, 674, 12 + 11 * len(lines), 0x000000, 70)
    c.polyline([(600, top), (1274, top), (1274, top + 12 + 11 * len(lines)), (600, top + 12 + 11 * len(lines)), (600, top)], 1, 0xC0A040)
    for i, line in enumerate(lines):
        c.text_line(605, top + 13 + 11 * i, line, Style(10.5), 0xE8D080)


# ------------------------------------------------------------------------------------------------------------
# One scenario
# ------------------------------------------------------------------------------------------------------------

def render(sc: Scenario, world: World, heal_frame: Image.Image, card_base: Image.Image, ctx: Ctx, ss: int,
           out_dir: Path) -> list[str]:
    tree = world.tree
    report = [f"== {sc.name}"]
    targets = {}
    for school, (spell, pct) in sc.targets.items():
        targets[school] = (tree.nodes[node_index(tree, spell, school)].form_id, pct)
    player = mm.PlayerState(world.known_ids(), targets, world.required_xp)
    snap = mm.build_snapshot(tree, player, [sc.school])
    list_node = node_index(tree, sc.list_spell, sc.school)
    selected = node_index(tree, sc.clicked, sc.school) if sc.clicked else -1
    centre = selected if selected >= 0 else list_node  # Widget.cpp:2292 a tree click centres on the clicked node

    frame = repaint_right(load_frame(sc.frame))
    big = frame.resize((1280 * ss, 800 * ss), Image.LANCZOS)
    c = Canvas(big, ss)
    # Widget.cpp:2627-2631: preview (opaque: over-pass on), tree, progress, node card; legend
    panel(c, PREVIEW, S["preview_title"], PREVIEW_ALPHA)
    panel(c, TREE, S["tree_title"], PANEL_ALPHA)
    panel(c, PROGRESS, None, PANEL_ALPHA)
    panel(c, NODE_CARD, None, PANEL_ALPHA)
    draw_legend(c, ctx)
    report.append(place_effect(big, ss, effect_crop(heal_frame, sc.effect_tint_green)))
    tr = redraw_tree(c, ctx, tree, snap, centre, selected)
    report += tr.lines
    redraw_progress(c, ctx, tree, snap, sc.school, report)
    redraw_node_card(c, ctx, tree, snap, selected, report)
    card_ss = 4.0
    spell = {"name": "회춘의 손길", "description": "가까운 생명체 대상의 체력을 5 회복합니다. 이후 소량의 체력을 다시 회복합니다.",
             "magnitude": "5", "level": "초보자", "cost": "24"}
    report.append(place_card(big, ss, build_card(card_base, sc.card, card_ss, spell), card_ss))
    annotate(big, ss, sc.notes)
    out_dir.mkdir(parents=True, exist_ok=True)
    two = big.resize((2560, 1600), Image.LANCZOS)
    one = big.resize((1280, 800), Image.LANCZOS)
    one.save(out_dir / f"{sc.name}.png")
    two.save(out_dir / f"{sc.name}@2x.png")
    report.append(f"saved {out_dir / (sc.name + '.png')} and @2x")
    return report


KNOWN_SPELLS = [  # the list rows of base_v3_poison.png that are tree nodes (name, saved FormID)
    ("느린 화염 고리", 0x1D0DE93A), ("독조각", 0x498C886D), ("드래곤파이어", 0xA51EEA7A), ("불꽃 망토", 0x0003AE9F),
    ("불꽃의 룬", 0x0005DB90), ("불꽃의 벽", 0x00035D7F), ("불씨조각", 0x00012FCD), ("소각", 0x0010F7ED),
    ("치명적인 독 분사", 0x499420A3), ("치유", 0x00012FCC), ("화염 망령", 0x1B35AF0A), ("화염 폭풍", 0x0007A82B),
    ("화염 화살", 0x00012FD0), ("화염구", 0x0001C789),
]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tree", type=Path, default=Path("D:/TAKEALOOK/overwrite/SKSE/Plugins/SpellLearning/spell_tree.json"))
    ap.add_argument("--video3", type=Path, default=Path("C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video3"))
    ap.add_argument("--out", type=Path, default=Path("C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video3/render"))
    ap.add_argument("--row-size", type=float, default=LIST_ROW_FALLBACK,
                    help="the list row size Size() uses: 14 = kListRowFallback (default); about 13.3 measured on the frame")
    ap.add_argument("--ss", type=int, default=4, help="pixels per stage unit while drawing (downsampled to 2 and 1)")
    args = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(name)s: %(message)s")
    for path in (args.tree, args.video3 / "expected/base_v3_poison.png", args.video3 / "frames/f_014.jpg"):
        if not path.exists():
            log.error("input missing: %s", path)
            return 2
    row = min(max(args.row_size, LIST_ROW_MIN), LIST_ROW_MAX)
    ctx = Ctx(row)
    world = World(mm.load_tree(args.tree), KNOWN_SPELLS, {"Novice": 100.0})
    poison = args.video3 / "expected/base_v3_poison.png"
    heal = args.video3 / "frames/f_014.jpg"
    heal_frame = load_frame(heal)
    card_base = load_frame(poison)
    common = [f"렌더 주석 (게임 화면 아님) · 글꼴 맑은 고딕 보정 · 읽기 크기 {row:g}"]
    scenarios = [
        Scenario("scenario1_all_poison_no_target", poison, "Restoration", "치명적인 독 분사", None, {}, True, "original",
                 common + ["미리보기: 독 분사 3D 캡처 없음 → 치유 구체를 초록으로 바꾼 대체 이미지"]),
        Scenario("scenario2_heal_click_rejuvenating", heal, "Restoration", "치유", "회춘의 손길", {}, False, "rejuvenating",
                 common + ["미리보기: 치유(목록 선택) 그대로 (bPreviewUnlearned=0) · 카드 매지카 24는 추정값"]),
        Scenario("scenario3_all_poison_target40", poison, "Restoration", "치명적인 독 분사", None,
                 {"Restoration": ("회춘의 손길", 40.0)}, True, "original",
                 common + ["미리보기: 독 분사 3D 캡처 없음 → 치유 구체를 초록으로 바꾼 대체 이미지"]),
    ]
    report: list[str] = []
    for sc in scenarios:
        for line in render(sc, world, heal_frame, card_base, ctx, args.ss, args.out):
            log.info("%s", line)
            report.append(line)
    (args.out / "render_log.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
