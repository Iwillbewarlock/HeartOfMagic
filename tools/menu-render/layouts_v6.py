"""Item card options on the build-6 layout (E2 without panel borders), scenario 3 (치명적인 독 분사 in the list,
learning target 회춘의 손길 40%).

    python layouts_v6.py [--out DIR]

E2-현재: build 6 as the source draws it (borderless E2 rects, PlanProgress, faint locked neighbours and "잠김 N" exits,
the node card's default target, the SkyUI card fitted at 0.686) with build 6's real preview (video6 f_029, the
Cylinder08 disc still there). F/G/H are proposals (marked PROPOSAL); all three hide the disc.
"""
from __future__ import annotations

import argparse
import logging
import math
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

import layouts as L
import menu_model as mm
import render_menu as r
from menu_model import State

log = logging.getLogger("menu_layouts_v6")

# Settings.h:45-49 (build 6 = E2)
E2 = dict(preview=(648, 67, 969, 297), tree=(978, 67, 1238, 297), progress=(648, 306, 969, 408),
          item_card=(648, 417, 969, 623), node_card=(978, 306, 1238, 623))
# Widget.cpp build 6
CARD_BOTTOM = 2.0           # kCardBottom
FAINT_LINK, FAINT_TEXT = 0x4A4A4A, 0x6A6A6A  # kFaintLink, kFaintText
LOCKED_EXIT_TEXT_W = 48.0   # kLockedExitTextW
LINK_STEP, LINK_HALF = 4.0, 1.0  # kLinkSampleStep, kLinkSampleHalf
NODE_BOTTOM = {"BUTTON_FROM_BOTTOM": 8.0 + 28.0, "XP_FROM_BOTTOM": 36.0 + 32.0, "BOTTOM_RULE_FROM_BOTTOM": 36.0 + 38.0}
# HoM config.json (D:/TAKEALOOK/overwrite/SKSE/Plugins/SpellLearning): xpApprentice 200, xpAdept 400, xpExpert 800,
# xpMaster 1500; Novice 100 from the build-3 frame f_014 ("XP 0 / 100" for 회춘의 손길)
REQUIRED_XP = {"Novice": 100.0, "Apprentice": 200.0, "Adept": 400.0, "Expert": 800.0, "Master": 1500.0}

POISON = {"name": "치명적인 독 분사", "school": "Restoration", "level": "숙련자", "cost": "52", "cast": "집중",
          "description": L.POISON_CARD["description"], "magnitude": "20"}


@dataclass
class Variant:
    key: str
    label: str
    rects: dict
    card: str            # "skyui" (SkyUI card at the code's fit), "own" (F), "none" (G)
    remove_disc: bool
    node_info: bool = False   # G: the node card shows the list selection with a "주문 정보" section
    note: str = ""


def apply(rects: dict) -> None:
    r.PREVIEW, r.TREE, r.PROGRESS, r.ITEM_CARD, r.NODE_CARD = (rects["preview"], rects["tree"], rects["progress"],
                                                               rects["item_card"], rects["node_card"])


def content(rect):  # Settings.h:76 PanelContent
    return rect[0] + r.PAD, rect[1] + r.TITLE_H + r.PAD, rect[2] - r.PAD, rect[3] - r.PAD


# ------------------------------------------------------------------------------------------------------------
# Preview: build 6's real poison model (video6 f_029)
# ------------------------------------------------------------------------------------------------------------

def poison_effect(frame6: Image.Image, remove_disc: bool) -> np.ndarray:
    """The E2 preview content area of f_029 (1280x800 after upscaling the 1152x720 frame). The Cylinder08 disc is the
    dark ring (luminance 8-20) around the orb (60-255): with remove_disc everything under luminance 28 goes to black,
    ramping to full at 58, which takes the disc and keeps the orb and its glow."""
    l, t, rr, b = content(E2["preview"])
    a = np.asarray(frame6.crop((l, t + 40, rr, b + 40))).astype(np.float64)
    if remove_disc:
        lum = a.mean(axis=2)
        a = a * np.clip((lum - 28) / 30.0, 0, 1)[..., None]
    return a


def place_poison(canvas: Image.Image, ss: float, effect: np.ndarray, preview: tuple) -> str:
    """Scaled with the box the way a bound-sphere fit scales (PreviewMath.h kFill of min(width, height)): factor
    min(w, h) of this content area over the E2 one; centred on the content centre, clipped to it."""
    l0, t0, r0, b0 = content(E2["preview"])
    l, t, rr, b = content(preview)
    k = min(rr - l, b - t) / min(r0 - l0, b0 - t0)
    img = Image.fromarray(effect.clip(0, 255).astype(np.uint8))
    w, h = img.width * k, img.height * k
    big = img.resize((round(w * ss), round(h * ss)), Image.LANCZOS)
    cx, cy = (l + rr) / 2, (t + b) / 2
    px, py = round((cx - w / 2) * ss), round((cy - h / 2 + 40) * ss)
    cl, ct, cr, cb = round(l * ss), round((t + 40) * ss), round(rr * ss), round((b + 40) * ss)
    sx0, sy0 = max(0, cl - px), max(0, ct - py)
    sx1, sy1 = min(big.width, cr - px), min(big.height, cb - py)
    region = np.asarray(big.crop((sx0, sy0, sx1, sy1))).astype(int)
    base = np.asarray(canvas.crop((px + sx0, py + sy0, px + sx1, py + sy1))).astype(int)
    canvas.paste(Image.fromarray(np.minimum(255, base + region).astype(np.uint8)), (px + sx0, py + sy0))
    return f"preview: build-6 poison model x {k:.2f} (sphere-fit rule), centred {cx:.1f},{cy:.1f}"


# ------------------------------------------------------------------------------------------------------------
# Build 6 progress card (Widget.cpp RedrawProgress + LayoutMath.h PlanProgress, text metrics at their 1.25 defaults)
# ------------------------------------------------------------------------------------------------------------

def progress_b6(c: r.Canvas, ctx: r.Ctx, tree: mm.Tree, snap: mm.Snapshot, school: str, node_card_top: float, out: list[str]) -> None:
    rect = r.PROGRESS
    w, h = rect[2] - rect[0] - 2 * r.PAD, rect[3] - rect[1]
    title_size, hint_size, bottom = ctx.size(r.SCHOOL_SIZE), ctx.size(r.HINT_SIZE), h - CARD_BOTTOM
    shift = node_card_top - rect[1] + r.te.BASELINE * (r.NAME_SIZE - title_size)
    offset = shift if 0 <= shift <= title_size else 0.0
    target, pct, required = snap.schools[school]
    t = r.tally(tree, snap, school)
    target_h = r.field_h(r.TARGET_SIZE)
    bar_row_h = max(r.BAR_H, r.field_h(r.XP_SIZE))
    title_h, smallest = r.field_h(title_size), r.field_h(8.0)
    y = r.CARD_TOP + offset + title_h
    if offset > 0 and y + target_h + bar_row_h + smallest > bottom:
        y -= offset
        offset = 0.0
    with c.at(rect[0] + r.PAD, rect[1]):
        r.fit_line(c, 0, r.CARD_TOP + offset, w, title_size, r.fill("progress_title", school=r.SCHOOL_TEXT[school]), r.TITLE_COLOR)
        if not target:
            line = r.next_step_line(t, False)
            r.fit_lines(c, 0, y, w, hint_size, r.MESSAGE_LINES, line, r.WHITE)
            out.append(f"progress (build 6): offset {offset:.1f} | {line}")
            return
        name = tree.nodes[mm.find(tree, target, school)].name
        final = r.fit_text(w, target_h, name, lambda n: r.fill("progress_target", spell=n, pct=f"{pct:.0f}"), r.TARGET_SIZE, False)
        r.draw_field(c, 0, y, w, target_h, final, r.WHITE, r.TARGET_SIZE)
        y += target_h
        bar_w = max(0.0, w - r.XP_W - r.PAD)
        bar_y = y + (bar_row_h - r.BAR_H) / 2
        c.rect_fill(0, bar_y, bar_w, r.BAR_H, r.BAR_BACK, 100)
        c.rect_fill(0, bar_y, bar_w * pct / 100, r.BAR_H, r.BLUE, 100)
        xp = r.fill("xp", cur=f"{required * pct / 100:.0f}", need=f"{required:.0f}")
        r.fit_line(c, bar_w + r.PAD, y + (bar_row_h - r.field_h(r.XP_SIZE)) / 2, r.XP_W, r.XP_SIZE, xp, r.WHITE, "right")
        y += bar_row_h
        size = hint_size
        while size - 0.5 >= 8.0 and r.field_h(size) > bottom - y:
            size -= 0.5
        nxt = r.next_step_line(t, True)
        r.fit_line(c, 0, y, w, size, nxt, r.LABEL_COLOR)
    out.append(f"progress (build 6): offset {offset:.1f} | {final} | {xp} | {nxt} (size {size:.1f})")


# ------------------------------------------------------------------------------------------------------------
# Build 6 tree extras: centre label side, faint locked neighbours, "잠김 N" exits
# ------------------------------------------------------------------------------------------------------------

def tree_b6(c: r.Canvas, ctx: r.Ctx, tree: mm.Tree, snap: mm.Snapshot, centre: int, out: list[str]) -> None:
    cx, cy, half_w, half_h = r.tree_content()
    school = mm.school_named(tree, tree.nodes[centre].school)
    rot = mm.school_rotation_deg(tree, school)
    nodes, edges = mm.query(tree, centre, rot, r.TREE_SCALE, half_w, half_h, r.MAX_STEPS, r.MAX_NODES)
    looks = {p.index: mm.look_of(tree, snap, p.index)[0] for p in nodes}
    drawable = [p for p in nodes if p.index == centre or looks[p.index] in mm.SHOWN_STATES]
    faint = [p for p in nodes if p.index != centre and looks[p.index] == State.LOCKED] if len(drawable) == 1 else []
    boxes = []
    for p in faint:  # the faint link sampled into small boxes and the faint node's own box, kept off by the centre label
        n = max(1, int(math.hypot(p.sx, p.sy) / LINK_STEP))
        boxes += [(p.sx * i / n - LINK_HALF, p.sy * i / n - LINK_HALF, p.sx * i / n + LINK_HALF, p.sy * i / n + LINK_HALF) for i in range(n + 1)]
        boxes.append((p.sx - r.HIT_R, p.sy - r.HIT_R, p.sx + r.HIT_R, p.sy + r.HIT_R))
    size = ctx.size(r.LABEL_SIZE)
    tw, th = r.text_wh(tree.nodes[centre].name, size, r.LABEL_W, True)
    gap = r.HIT_R + r.LABEL_GAP
    right = (gap, -th / 2, gap + min(tw, r.LABEL_W), th / 2)
    hits = any(right[0] < b[2] and b[0] < right[2] and right[1] < b[3] and b[1] < right[3] for b in boxes)
    r.LABEL_SIDE.clear()
    r.LABEL_SIDE[centre] = hits  # "the right when both sides are clear"
    # locked exits: linked locked spells outside the window, per 45-degree sector (faint arrow, "잠김 N")
    inside = {p.index for p in nodes}
    sectors = [0] * 8
    f = tree.nodes[centre]
    rad = math.radians(rot)
    cs, sn = math.cos(rad), math.sin(rad)
    for e in tree.adjacent[centre]:
        edge = tree.edges[e]
        other = edge.b if edge.a == centre else edge.a
        if other in inside or mm.look_of(tree, snap, other)[0] != State.LOCKED or not tree.nodes[other].has_xy:
            continue
        dx, dy = tree.nodes[other].x - f.x, tree.nodes[other].y - f.y
        ang = math.atan2(dx * sn + dy * cs, dx * cs - dy * sn)
        sectors[(round(ang / (math.pi / 4)) % 8 + 8) % 8] += 1
        out.append(f"locked exit: {tree.nodes[other].name}")
    with c.at(r.TREE[0] + cx, r.TREE[1] + cy):
        for pe in edges:
            e = tree.edges[pe.edge]
            ids = {p.index for p in faint} | {centre}
            if faint and e.a in ids and e.b in ids:
                c.polyline([(pe.ax, pe.ay), (pe.bx, pe.by)], r.LINE_W, FAINT_LINK)
        for p in faint:
            c.polyline(r.diamond_pts(p.sx, p.sy, r.NODE_R), r.LINE_W, r.LOCKED_EDGE)
            left = p.sx < 0
            box_h = r.field_h(size, 3)
            final = r.fit_text(r.LABEL_W, box_h, tree.nodes[p.index].name, lambda n: n + "\r잠김", size, True)
            _, fh = r.text_wh(final, size, r.LABEL_W, True)
            x = p.sx - gap - r.LABEL_W if left else p.sx + gap
            r.draw_field(c, x, p.sy - fh / 2, r.LABEL_W, box_h, final, FAINT_TEXT, size, "right" if left else "left", wrap=True)
        h = r.field_h(size)
        hw, hh = half_w - r.EXIT_INSET, half_h - r.EXIT_INSET
        for i, n in enumerate(sectors):
            if not n:
                continue
            ang = i * math.pi / 4
            ux, uy = math.cos(ang), math.sin(ang)
            tt = min(hw / abs(ux) if abs(ux) > 1e-9 else 1e9, hh / abs(uy) if abs(uy) > 1e-9 else 1e9)
            px, py, a = ux * tt, uy * tt, r.EXIT_ARROW
            c.polygon([(px + ux * a, py + uy * a), (px - ux * a - uy * a, py - uy * a + ux * a), (px - ux * a + uy * a, py - uy * a - ux * a)], FAINT_TEXT)
            tx = min(max(px - ux * (a + LOCKED_EXIT_TEXT_W / 2), -half_w + LOCKED_EXIT_TEXT_W / 2), half_w - LOCKED_EXIT_TEXT_W / 2)
            ty = min(max(py - uy * (a + h / 2), -half_h - r.PAD + h / 2), half_h + r.PAD - h / 2)
            r.fit_line(c, tx - LOCKED_EXIT_TEXT_W / 2, ty - h / 2, LOCKED_EXIT_TEXT_W, size, f"잠김 {n}", FAINT_TEXT, "center")
    out.append(f"faint {[tree.nodes[p.index].name for p in faint]}, centre label {'left' if hits else 'right'}, locked exits {sectors}")


# ------------------------------------------------------------------------------------------------------------
# F: our own info card / G: info section in the node card
# ------------------------------------------------------------------------------------------------------------

def info_block(c: r.Canvas, ctx: r.Ctx, w: float, y: float, spell: dict, max_lines: int, out: list[str]) -> float:
    """PROPOSAL: the description (reading size, wrapped, "..." past max_lines) and the cost / cast type row."""
    size = ctx.size(r.ROW_SIZE)
    _, dh = r.fit_lines(c, 0, y, w, size, max_lines, spell["description"], r.LABEL_COLOR)
    y += min(dh, r.field_h(size, max_lines)) + r.RULE_GAP
    tw, _ = r.fit_line(c, 0, y, w, size, "매지카", r.DIM_TEXT)
    r.fit_line(c, tw - 2, y, w - tw, size, spell["cost"], r.WHITE)
    r.fit_line(c, 0, y, w, size, spell["cast"], r.DIM_TEXT, "right")
    out.append(f"info: {spell['name']} | {spell['cost']} | {spell['cast']}")
    return y + r.field_h(size)


def own_card(c: r.Canvas, ctx: r.Ctx, spell: dict, out: list[str]) -> None:
    """PROPOSAL (F): our own card in the item card slot, panel style (85% black, no border), node-card-like heading."""
    rect = r.ITEM_CARD
    r.panel(c, rect, None, r.PANEL_ALPHA)
    w, h = rect[2] - rect[0] - 2 * r.PAD, rect[3] - rect[1]
    with c.at(rect[0] + r.PAD, rect[1]):
        y = r.CARD_TOP
        r.fit_line(c, 0, y, w, r.NAME_SIZE, spell["name"], r.WHITE)
        y += r.field_h(r.NAME_SIZE)
        sub = ctx.size(r.SUB_SIZE)
        r.fit_line(c, 0, y, w, sub, r.fill("node_sub", school=r.SCHOOL_TEXT[spell["school"]], level=spell["level"]), r.TITLE_COLOR)
        y += r.field_h(sub) + r.RULE_GAP
        c.polyline([(0, y), (w, y)], r.EDGE_W, r.PANEL_EDGE)
        size = ctx.size(r.ROW_SIZE)
        bottom_row = h - r.PAD - r.field_h(size)
        lines = max(1, min(4, int((bottom_row - r.RULE_GAP * 2 - y) // (r.LINE_FACTOR * size))))
        r.fit_lines(c, 0, y + r.RULE_GAP, w, size, lines, spell["description"], r.LABEL_COLOR)
        c.polyline([(0, bottom_row - r.RULE_GAP), (w, bottom_row - r.RULE_GAP)], r.EDGE_W, r.PANEL_EDGE)
        tw, _ = r.fit_line(c, 0, bottom_row, w, size, "매지카", r.DIM_TEXT)
        r.fit_line(c, tw - 2, bottom_row, w - tw, size, spell["cost"], r.WHITE)
        r.fit_line(c, 0, bottom_row, w, size, spell["cast"], r.DIM_TEXT, "right")
    out.append(f"own card: {spell['name']}, description up to {lines} lines at size {size:g}")


def node_card_info(c: r.Canvas, ctx: r.Ctx, tree: mm.Tree, snap: mm.Snapshot, index: int, spell: dict, out: list[str]) -> None:
    """PROPOSAL (G): build 5/6's node card for the list selection with a "주문 정보" section between its heading and its
    learning details; the bottom block as build 6 (button bottom at the card bottom minus kPad)."""
    rect = r.NODE_CARD
    w, h = rect[2] - rect[0] - 2 * r.PAD, rect[3] - rect[1]
    node = tree.nodes[index]
    state, pct = mm.look_of(tree, snap, index)
    with c.at(rect[0] + r.PAD, rect[1]):
        y = r.CARD_TOP
        r.fit_line(c, 0, y, w, r.NAME_SIZE, node.name, r.WHITE)
        y += r.field_h(r.NAME_SIZE)
        sub = ctx.size(r.SUB_SIZE)
        r.fit_line(c, 0, y, w, sub, r.fill("node_sub", school=r.SCHOOL_TEXT[node.school], level=r.LEVEL_TEXT[node.skill_level]), r.TITLE_COLOR)
        y += r.field_h(sub) + r.RULE_GAP
        c.polyline([(0, y), (w, y)], r.EDGE_W, r.PANEL_EDGE)
        y += r.RULE_GAP
        r.fit_line(c, 0, y, w, sub, "주문 정보", r.TITLE_COLOR)
        y += r.field_h(sub)
        y = info_block(c, ctx, w, y, spell, 4, out) + r.RULE_GAP
        c.polyline([(0, y), (w, y)], r.EDGE_W, r.PANEL_EDGE)
        y += r.RULE_GAP
        ss_ = ctx.size(r.STATE_SIZE)
        sh = r.field_h(ss_)
        r.draw_mark(c, r.STATE_ICON_R + r.RING_GAP, y + sh / 2, r.STATE_ICON_R, state, pct)
        st = r.state_line(state, pct, False)
        r.fit_line(c, r.STATE_TEXT_X, y, w - r.STATE_TEXT_X, ss_, st, r.WHITE)
        y += sh + r.RULE_GAP
        bottom_rule = h - NODE_BOTTOM["BOTTOM_RULE_FROM_BOTTOM"]
        r.draw_rows(c, ctx, r.requirement_rows(tree, snap, node), w, y, bottom_rule - r.RULE_GAP, out)
        c.polyline([(0, bottom_rule), (w, bottom_rule)], r.EDGE_W, r.PANEL_EDGE)
        required = snap.spells[node.form_id].required
        xp = r.fill("xp", cur=f"{required * pct / 100:.0f}", need=f"{required:.0f}")
        r.fit_line(c, 0, h - NODE_BOTTOM["XP_FROM_BOTTOM"], w, ss_, xp, r.LABEL_COLOR, "right")
        with c.at(0, h - NODE_BOTTOM["BUTTON_FROM_BOTTOM"]):
            c.rect_fill(0, 0, w, r.BUTTON_H, r.BUTTON_OFF, 100)
            c.polyline([(0, 0), (w, 0), (w, r.BUTTON_H), (0, r.BUTTON_H), (0, 0)], r.EDGE_W, r.LOCKED_EDGE)
            bs = ctx.size(r.BUTTON_SIZE)
            r.fit_line(c, 0, (r.BUTTON_H - r.field_h(bs)) / 2, w, bs, r.state_line(state, pct, True), r.DIM_TEXT, "center")
    out.append(f"node card (G): {node.name} | {st} | {xp}")


# ------------------------------------------------------------------------------------------------------------
# H: the slot that gives SkyUI's card the biggest fit
# ------------------------------------------------------------------------------------------------------------

def card_slot_table() -> list[tuple[str, tuple, float]]:
    """Every E2 box as the card's slot, scored by ComputeCardFit (min(w/468, h/300)); the merged left row-2 area too."""
    rows = [("현재 카드 칸", E2["item_card"]), ("미리보기 자리", E2["preview"]), ("트리 자리", E2["tree"]),
            ("노드 카드 자리", E2["node_card"]), ("왼쪽 2행 전체 (진도+카드)", (648, 306, 969, 623))]
    return [(name, rect, L.card_fit(rect)[0]) for name, rect in rows]


VARIANTS = [
    Variant("E2now", "E2-현재 (빌드 6)", E2, "skyui", False),
    Variant("F", "F. 새 정보창 (자체 카드)", E2, "own", True),
    Variant("G", "G. 정보창을 노드 카드에 합치기",
            dict(preview=(648, 67, 969, 512), tree=(978, 67, 1238, 297), progress=(648, 521, 969, 623),
                 item_card=(648, 521, 969, 623), node_card=(978, 306, 1238, 623)), "none", True, node_info=True),
    Variant("H", "H. SkyUI 카드 유지 (가장 큰 자리 = 현재 칸, 0.686)", E2, "skyui", True),
]


def render_variant(v: Variant, world: r.World, frame3: Path, frame6: Image.Image, card_base: Image.Image, ctx: r.Ctx,
                   ss: int, out_dir: Path) -> list[str]:
    apply(v.rects)
    tree = world.tree
    school = "Restoration"
    target = tree.nodes[r.node_index(tree, "회춘의 손길", school)]
    player = mm.PlayerState(world.known_ids(), {school: (target.form_id, 40.0)}, world.required_xp)
    snap = mm.build_snapshot(tree, player, [school])
    centre = r.node_index(tree, "치명적인 독 분사", school)
    report = [f"== {v.key}: {v.label}", "rects " + ", ".join(f"{k} {val}" for k, val in v.rects.items())]
    big = r.repaint_right(r.load_frame(frame3)).resize((1280 * ss, 800 * ss), Image.LANCZOS)
    c = r.Canvas(big, ss)
    r.panel(c, r.PREVIEW, r.S["preview_title"], r.PREVIEW_ALPHA)
    r.panel(c, r.TREE, r.S["tree_title"], r.PANEL_ALPHA)
    r.panel(c, r.PROGRESS, None, r.PANEL_ALPHA)
    r.panel(c, r.NODE_CARD, None, r.PANEL_ALPHA)
    r.draw_legend(c, ctx)
    report.append(place_poison(big, ss, poison_effect(frame6, v.remove_disc), r.PREVIEW))
    tree_b6(c, ctx, tree, snap, centre, report)
    report += r.redraw_tree(c, ctx, tree, snap, centre, -1).lines
    progress_b6(c, ctx, tree, snap, school, r.NODE_CARD[1], report)
    saved = {k: getattr(r, k) for k in NODE_BOTTOM}
    for k, val in NODE_BOTTOM.items():
        setattr(r, k, val)
    try:
        if v.node_info:
            node_card_info(c, ctx, tree, snap, centre, POISON, report)
        else:
            r.redraw_node_card(c, ctx, tree, snap, tree.nodes.index(target), report)
    finally:
        for k, val in saved.items():
            setattr(r, k, val)
    if v.card == "skyui":
        scale, left, top, width = L.card_fit(r.ITEM_CARD)
        r.CARD_STAGE_LEFT, r.CARD_STAGE_TOP, r.CARD_STAGE_W = left, top, width
        report.append(f"SkyUI card fit: holder scale {scale:.3f}, background {left:.1f},{top:.1f} width {width:.1f}")
        report.append(r.place_card(big, ss, r.build_card(card_base, "redraw", 4.0, L.POISON_CARD), 4.0))
    elif v.card == "own":
        own_card(c, ctx, POISON, report)
    L.label_strip(c, v.label)
    notes = [f"렌더 주석 (게임 화면 아님) · {v.label} · 장면: 치명적인 독 분사 선택, 학습 대상 회춘의 손길 40%",
             "미리보기: 빌드 6 실제 독 분사 모델 (video6 f_029)" + (" · 뒤 원반(Cylinder08)은 밝기 기준으로 지움" if v.remove_disc else " · 원반 그대로 (빌드 6)")]
    if v.key == "H":
        notes.append("카드 칸 후보: " + " / ".join(f"{n} {s:.3f}" for n, _, s in card_slot_table()))
    r.annotate(big, ss, notes)
    out_dir.mkdir(parents=True, exist_ok=True)
    name = f"layout_{v.key}"
    big.resize((2560, 1600), Image.LANCZOS).save(out_dir / f"{name}@2x.png")
    big.resize((1280, 800), Image.LANCZOS).save(out_dir / f"{name}.png")
    report.append(f"saved {out_dir / (name + '.png')} and @2x")
    return report


def compare_2x2(out_dir: Path, keys: list[str], labels: list[str], name: str) -> Path:
    from PIL import ImageDraw, ImageFont
    tiles = [Image.open(out_dir / f"layout_{k}@2x.png").crop(tuple(v * 2 for v in L.SHEET_CROP)) for k in keys]
    tw, th = tiles[0].size
    label_h, pad = 70, 20
    sheet = Image.new("RGB", (2 * tw + 3 * pad, 2 * (th + label_h) + 3 * pad), (18, 18, 18))
    font = ImageFont.truetype(str(r.te.FONT_PATH), 40)
    d = ImageDraw.Draw(sheet)
    for i, (img, label) in enumerate(zip(tiles, labels)):
        x, y = pad + (i % 2) * (tw + pad), pad + (i // 2) * (th + label_h + pad)
        sheet.paste(img, (x, y))
        d.text((x + 10, y + th + 12), label, font=font, fill=(240, 216, 144))
    path = out_dir / name
    sheet.save(path)
    return path


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tree", type=Path, default=Path("D:/TAKEALOOK/overwrite/SKSE/Plugins/SpellLearning/spell_tree.json"))
    ap.add_argument("--video3", type=Path, default=Path("C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video3"))
    ap.add_argument("--frame6", type=Path, default=Path("C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video6/frames/f_029.jpg"))
    ap.add_argument("--out", type=Path, default=Path("C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video3/render/layouts"))
    ap.add_argument("--row-size", type=float, default=r.LIST_ROW_FALLBACK)
    ap.add_argument("--ss", type=int, default=4)
    args = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(name)s: %(message)s")
    frame3 = args.video3 / "expected/base_v3_poison.png"
    for path in (args.tree, frame3, args.frame6):
        if not path.exists():
            log.error("input missing: %s", path)
            return 2
    r.PANEL_BORDER = False
    ctx = r.Ctx(min(max(args.row_size, r.LIST_ROW_MIN), r.LIST_ROW_MAX))
    world = r.World(mm.load_tree(args.tree), r.KNOWN_SPELLS, REQUIRED_XP)
    frame6, card_base = r.load_frame(args.frame6), r.load_frame(frame3)
    report = [f"card slot candidates: {[(n, rect, round(s, 3)) for n, rect, s in card_slot_table()]}"]
    for v in VARIANTS:
        for line in render_variant(v, world, frame3, frame6, card_base, ctx, args.ss, args.out):
            log.info("%s", line)
            report.append(line)
    report.append("sheet " + str(compare_2x2(args.out, [v.key for v in VARIANTS], [v.label for v in VARIANTS], "compare_FGH.png")))
    (args.out / "layouts_v6_log.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
