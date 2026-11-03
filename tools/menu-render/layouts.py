"""Layout options for SL_MagicMenu, rendered with render_menu's drawing code (scenario 3: 치명적인 독 분사 in the list,
learning target 회춘의 손길 40%).

    python layouts.py [--out DIR]   (other inputs as render_menu.py)

A is build 5 as it is. B, C, D are proposals: the rects and the extra drawing marked PROPOSAL below are not in the
plugin. Every panel keeps build 5's own drawing; only where it sits (and, for D, two behaviours) changes.
"""
from __future__ import annotations

import argparse
import logging
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

import menu_model as mm
import render_menu as r
from menu_model import State
from textengine import Style

log = logging.getLogger("menu_layouts")

POISON_CARD = {
    "name": "치명적인 독 분사",
    "description": "초당 20의 독 피해를 줍니다. 중독된 대상은 시간이 지남에 따라 추가 피해를 받습니다. "
                   "초당 20의 독 피해를 줍니다. 중독된 목표는 지속적인 추가 피해를 입습니다.",
    "magnitude": "20", "level": "숙련자", "cost": "52",
}
CARD_BG_HOLDER = (468.0, 300.0)  # log: "background 468x300 in the holder"; its width = the frame's black 424 px
CARD_FRAME_PER_HOLDER = 424.0 / 468.0


@dataclass
class Layout:
    key: str
    label: str
    preview: tuple[float, float, float, float]
    tree: tuple[float, float, float, float]
    progress: tuple[float, float, float, float]
    item_card: tuple[float, float, float, float]
    node_card: tuple[float, float, float, float]
    progress_strip: bool = False      # B: one wide row instead of build 5's stacked card
    node_card_target: bool = False    # D: nothing clicked -> the learning target's details
    faint_locked: bool = False        # D: a lone node's locked neighbours drawn faintly
    align_baselines: bool = False     # E2: progress card's first baseline on the node card's name baseline
    overrides: dict[str, float] = field(default_factory=dict)  # render_menu constants changed for this layout
    notes: list[str] = field(default_factory=list)


LAYOUTS = [
    Layout("A", "A. 현재 배치 (v8 목업, 빌드 5)",
           (648, 67, 957, 330), (971, 67, 1238, 330), (648, 339, 969, 441), (648, 450, 969, 623), (979, 339, 1238, 623)),
    Layout("B", "B. 진도 카드를 맨 위로",
           (648, 146, 957, 330), (971, 146, 1238, 330), (648, 67, 1238, 137), (648, 339, 969, 623), (979, 339, 1238, 623),
           progress_strip=True),
    Layout("C", "C. 설명칸 크게",
           (648, 67, 957, 260), (971, 67, 1238, 330), (648, 269, 969, 371), (648, 380, 969, 623), (979, 339, 1238, 623)),
    Layout("D", "D. 노드 카드 채우기 + 트리 빈칸 해결",
           (648, 67, 957, 330), (971, 67, 1238, 330), (648, 339, 969, 441), (648, 450, 969, 623), (979, 339, 1238, 623),
           node_card_target=True, faint_locked=True),
]


# E = C's column with D's content. C left 37 units empty under the card (fitted height 205.8 in a 243 slot); here the
# card slot is the fitted height (321 x 206: min(321/468, 206/300) = 0.686, the same scale as C) and the freed space
# goes to the preview (193 -> 230 high), panel gaps kept at 9.
LAYOUT_E = Layout("E", "E. C+D (설명칸 크게 + 노드 카드 채우기)",
                  (648, 67, 957, 297), (971, 67, 1238, 330), (648, 306, 969, 408), (648, 417, 969, 623), (979, 339, 1238, 623),
                  node_card_target=True, faint_locked=True)


# E2: E on one two-column grid, one gap (fPanelGap 9) everywhere. Left column 648..969, right 978..1238; row 1 67..297
# (preview, tree), row 2 306..623 (progress 306..408 + card slot 417..623 | node card 306..623). The node card's bottom
# block moves 2 units down so its button ends at 623 - kPad = 615, the card slot's inner bottom.
LAYOUT_E2 = Layout("E2", "E2. E 오와 열 맞춤",
                   (648, 67, 969, 297), (978, 67, 1238, 297), (648, 306, 969, 408), (648, 417, 969, 623), (978, 306, 1238, 623),
                   node_card_target=True, faint_locked=True, align_baselines=True,
                   overrides={"BUTTON_FROM_BOTTOM": 36.0, "XP_FROM_BOTTOM": 68.0, "BOTTOM_RULE_FROM_BOTTOM": 74.0})


def apply(layout: Layout) -> None:
    """render_menu's drawing functions read these module rects."""
    r.PREVIEW, r.TREE, r.PROGRESS, r.ITEM_CARD, r.NODE_CARD = (layout.preview, layout.tree, layout.progress,
                                                               layout.item_card, layout.node_card)


def card_fit(slot: tuple[float, float, float, float]) -> tuple[float, float, float, float]:
    """Widget.cpp:955-984 ComputeCardFit on the background bounds: (holder scale, left, top, stage width)."""
    w, h = slot[2] - slot[0], slot[3] - slot[1]
    scale = min(w / CARD_BG_HOLDER[0], h / CARD_BG_HOLDER[1])
    width = CARD_BG_HOLDER[0] * scale
    return scale, slot[0] + (w - width) / 2, slot[1], width


def progress_strip(c: r.Canvas, ctx: r.Ctx, tree: mm.Tree, snap: mm.Snapshot, school: str, out: list[str]) -> None:
    """PROPOSAL (B): build 5's progress lines in one wide row - school and target on the left, bar with XP and the
    next step on the right. Sizes, colours and strings as RedrawProgress (Widget.cpp:1972-2030)."""
    l, t, rr, b = r.PROGRESS
    w = rr - l - 2 * r.PAD
    left_w = w * 0.5
    right_x = left_w + 13
    right_w = w - right_x
    target, pct, required = snap.schools[school]
    t_ = r.tally(tree, snap, school)
    with c.at(l + r.PAD, t):
        title_size, hint_size = ctx.size(r.SCHOOL_SIZE), ctx.size(r.HINT_SIZE)
        y = r.CARD_TOP
        r.fit_line(c, 0, y, left_w, title_size, r.fill("progress_title", school=r.SCHOOL_TEXT[school]), r.TITLE_COLOR)
        y += r.field_h(title_size)
        name = tree.nodes[mm.find(tree, target, school)].name
        target_h = r.field_h(r.TARGET_SIZE)
        final = r.fit_text(left_w, target_h, name, lambda n: r.fill("progress_target", spell=n, pct=f"{pct:.0f}"), r.TARGET_SIZE, False)
        r.draw_field(c, 0, y, left_w, target_h, final, r.WHITE, r.TARGET_SIZE)
        xp_size = ctx.size(r.XP_SIZE)
        xp_h = r.field_h(xp_size)
        bar_y = 14.0
        bar_w = right_w - r.XP_W - r.PAD
        c.rect_fill(right_x, bar_y, bar_w, r.BAR_H, r.BAR_BACK, 100)
        c.rect_fill(right_x, bar_y, bar_w * pct / 100, r.BAR_H, r.BLUE, 100)
        xp = r.fill("xp", cur=f"{required * pct / 100:.0f}", need=f"{required:.0f}")
        r.fit_line(c, right_x + bar_w + r.PAD, bar_y + r.BAR_H / 2 - xp_h / 2, r.XP_W, xp_size, xp, r.WHITE, "right")
        nxt = r.next_step_line(t_, True)
        r.fit_line(c, right_x, bar_y + r.BAR_H + r.HINT_GAP + 6, right_w, hint_size, nxt, r.LABEL_COLOR)
    out.append(f"progress strip: {final} | {xp} | {nxt}")


FAINT_EDGE, FAINT_NODE, FAINT_TEXT = 0x4A4A4A, 0x6A6A6A, 0x6A6A6A  # PROPOSAL colours (kLockedEdge 6A6A6A for the node)


def faint_locked(c: r.Canvas, ctx: r.Ctx, tree: mm.Tree, snap: mm.Snapshot, centre: int, out: list[str]) -> None:
    """PROPOSAL (D): when the window would draw only its centre, its locked neighbours in the window are drawn first
    (under build 5's drawing) as grey outline diamonds with the name and "잠김"."""
    cx, cy, half_w, half_h = r.tree_content()
    school = mm.school_named(tree, tree.nodes[centre].school)
    rot = mm.school_rotation_deg(tree, school)
    nodes, edges = mm.query(tree, centre, rot, r.TREE_SCALE, half_w, half_h, r.MAX_STEPS, r.MAX_NODES)
    looks = {p.index: mm.look_of(tree, snap, p.index)[0] for p in nodes}
    drawable = [p for p in nodes if p.index == centre or looks[p.index] in mm.SHOWN_STATES]
    if len(drawable) != 1:
        return
    faint = [p for p in nodes if p.index != centre and looks[p.index] == State.LOCKED]
    ids = {p.index for p in faint} | {centre}
    size = ctx.size(r.LABEL_SIZE)
    with c.at(r.TREE[0] + cx, r.TREE[1] + cy):
        for pe in edges:
            e = tree.edges[pe.edge]
            if e.a in ids and e.b in ids:
                c.polyline([(pe.ax, pe.ay), (pe.bx, pe.by)], r.LINE_W, FAINT_EDGE)
        for p in faint:
            c.polyline(r.diamond_pts(p.sx, p.sy, r.NODE_R), r.LINE_W, FAINT_NODE)
            box_h = r.field_h(size, 3)
            left = p.sx < 0  # away from the centre, so the faint line does not run through the label
            gap = r.HIT_R + r.LABEL_GAP
            final = r.fit_text(r.LABEL_W, box_h, tree.nodes[p.index].name, lambda n: n + "\r잠김", size, True)
            _, th = r.text_wh(final, size, r.LABEL_W, True)
            x = p.sx - gap - r.LABEL_W if left else p.sx + gap
            r.draw_field(c, x, p.sy - th / 2, r.LABEL_W, box_h, final, FAINT_TEXT, size, "right" if left else "left", wrap=True)
            out.append(f"faint locked neighbour: {tree.nodes[p.index].name} at ({p.sx:.1f}, {p.sy:.1f})")


def label_strip(c: r.Canvas, text: str) -> None:
    """The variant's name above the panels (not part of any layout)."""
    st = Style(13.0)
    from textengine import width
    w = width(text, st) + 12
    c.rect_fill(648, 22, w, 22, 0x000000, 75)
    c.polyline([(648, 22), (648 + w, 22), (648 + w, 44), (648, 44), (648, 22)], 1, 0xC0A040)
    c.text_line(654, 38, text, st, 0xF0D890)


def render_layout(layout: Layout, world: r.World, frame_path: Path, heal_frame: Image.Image, card_base: Image.Image,
                  ctx: r.Ctx, ss: int, out_dir: Path) -> list[str]:
    apply(layout)
    tree = world.tree
    school = "Restoration"
    target = tree.nodes[r.node_index(tree, "회춘의 손길", school)]
    player = mm.PlayerState(world.known_ids(), {school: (target.form_id, 40.0)}, world.required_xp)
    snap = mm.build_snapshot(tree, player, [school])
    centre = r.node_index(tree, "치명적인 독 분사", school)
    report = [f"== layout {layout.key}: {layout.label}",
              f"rects preview {layout.preview} tree {layout.tree} progress {layout.progress} item card slot {layout.item_card} node card {layout.node_card}"]
    big = r.repaint_right(r.load_frame(frame_path)).resize((1280 * ss, 800 * ss), Image.LANCZOS)
    c = r.Canvas(big, ss)
    r.panel(c, r.PREVIEW, r.S["preview_title"], r.PREVIEW_ALPHA)
    r.panel(c, r.TREE, r.S["tree_title"], r.PANEL_ALPHA)
    r.panel(c, r.PROGRESS, None, r.PANEL_ALPHA)
    r.panel(c, r.NODE_CARD, None, r.PANEL_ALPHA)
    r.draw_legend(c, ctx)
    report.append(r.place_effect(big, ss, r.effect_crop(heal_frame, True)))
    if layout.faint_locked:
        faint_locked(c, ctx, tree, snap, centre, report)
    report += r.redraw_tree(c, ctx, tree, snap, centre, -1).lines
    if layout.progress_strip:
        progress_strip(c, ctx, tree, snap, school, report)
    else:
        top = r.CARD_TOP
        if layout.align_baselines:  # same field top, different sizes: move the smaller first line down to share the baseline
            top += te_baseline_gap(r.NAME_SIZE, ctx.size(r.SCHOOL_SIZE))
        r.redraw_progress(c, ctx, tree, snap, school, report, top)
    selected = tree.nodes.index(target) if layout.node_card_target else -1
    saved = {k: getattr(r, k) for k in layout.overrides}
    for k, v in layout.overrides.items():
        setattr(r, k, v)
    try:
        r.redraw_node_card(c, ctx, tree, snap, selected, report)
    finally:
        for k, v in saved.items():
            setattr(r, k, v)
    scale, left, top, width = card_fit(layout.item_card)
    r.CARD_STAGE_LEFT, r.CARD_STAGE_TOP, r.CARD_STAGE_W = left, top, width
    report.append(f"item card fit: holder scale {scale:.3f} (build 5 A: 0.577), background {left:.1f},{top:.1f} width {width:.1f} "
                  f"height {CARD_BG_HOLDER[1] * scale:.1f}")
    report.append(r.place_card(big, ss, r.build_card(card_base, "redraw", 4.0, POISON_CARD), 4.0))
    label_strip(c, layout.label)
    r.annotate(big, ss, [f"렌더 주석 (게임 화면 아님) · {layout.label} · 장면: 치명적인 독 분사 선택, 학습 대상 회춘의 손길 40%",
                         "미리보기: 독 분사 3D 캡처 없음 → 치유 구체를 초록으로 바꾼 대체 이미지 · 카드 글자는 실제 카드 서식으로 다시 그림"] + layout.notes)
    out_dir.mkdir(parents=True, exist_ok=True)
    name = f"layout_{layout.key}"
    big.resize((2560, 1600), Image.LANCZOS).save(out_dir / f"{name}@2x.png")
    big.resize((1280, 800), Image.LANCZOS).save(out_dir / f"{name}.png")
    report.append(f"saved {out_dir / (name + '.png')} and @2x")
    return report


SHEET_CROP = (640, 60, 1250, 680)  # in the 1280x800 picture


def compare_sheet(out_dir: Path) -> Path:
    """The four right halves in a 2x2 grid (each tile from the @2x picture, 1220 px wide), label under each."""
    tiles = []
    for layout in LAYOUTS:
        img = Image.open(out_dir / f"layout_{layout.key}@2x.png").crop(tuple(v * 2 for v in SHEET_CROP))
        tiles.append((layout.label, img))
    tw, th = tiles[0][1].size
    label_h, pad = 70, 20
    sheet = Image.new("RGB", (2 * tw + 3 * pad, 2 * (th + label_h) + 3 * pad), (18, 18, 18))
    font = ImageFont.truetype(str(r.te.FONT_PATH), 40)
    draw = ImageDraw.Draw(sheet)
    for i, (label, img) in enumerate(tiles):
        x = pad + (i % 2) * (tw + pad)
        y = pad + (i // 2) * (th + label_h + pad)
        sheet.paste(img, (x, y))
        draw.text((x + 10, y + th + 12), label, font=font, fill=(240, 216, 144))
    path = out_dir / "compare_layouts.png"
    sheet.save(path)
    return path


def te_baseline_gap(big: float, small: float) -> float:
    return r.te.BASELINE * (big - small)


def first_baseline(field_top: float, size: float) -> float:
    return field_top + r.te.GUTTER + r.te.BASELINE * size


def guides(layout: Layout, ctx: r.Ctx, out_dir: Path) -> list[str]:
    """layout_<key>_guides(.png, @2x): cyan lines on the column/row edges, the padding insets and the shared lines."""
    pv, tr, pg, ic, nc = layout.preview, layout.tree, layout.progress, layout.item_card, layout.node_card
    xs = sorted({pv[0], pv[2], tr[0], tr[2], pv[0] + r.PAD, tr[0] + r.PAD, pv[2] - r.PAD, tr[2] - r.PAD})
    ys = sorted({pv[1], pv[3], pg[1], pg[3], ic[1], ic[3]})
    title_base = first_baseline(pv[1] + r.PAD / 2, r.TITLE_SIZE)
    row2_base = first_baseline(nc[1] + r.CARD_TOP, r.NAME_SIZE)
    legend_mid = tr[3] - r.TREE_LEGEND_H / 2 - r.PAD / 2
    button_bottom = nc[3] - layout.overrides.get("BUTTON_FROM_BOTTOM", r.BUTTON_FROM_BOTTOM) + r.BUTTON_H
    special = [("titles baseline", title_base), ("row 2 first baseline", row2_base), ("legend centre", legend_mid),
               ("button bottom = card slot inner bottom", button_bottom)]
    lines = [f"x {xs}", f"y {ys}"] + [f"{n} {v:.1f}" for n, v in special]
    for suffix, ss in (("", 1), ("@2x", 2)):
        img = Image.open(out_dir / f"layout_{layout.key}{suffix}.png").convert("RGB")
        d = ImageDraw.Draw(img)
        y0, y1 = (40 + 50) * ss, (40 + 640) * ss
        for x in xs:
            d.line([(x * ss, y0), (x * ss, y1)], fill=(0, 229, 255), width=1)
        for y in ys:
            d.line([(630 * ss, (y + 40) * ss), (1250 * ss, (y + 40) * ss)], fill=(0, 229, 255), width=1)
        for _, y in special:
            for xa in range(630 * ss, 1250 * ss, 6 * ss):  # dashed: the shared text and button lines
                d.line([(xa, (y + 40) * ss), (xa + 3 * ss, (y + 40) * ss)], fill=(0, 229, 255), width=1)
        img.save(out_dir / f"layout_{layout.key}_guides{suffix}.png")
    return lines


def compare_row(out_dir: Path, keys: list[str], labels: list[str], name: str) -> Path:
    """Right halves side by side (each tile from the @2x picture, 1220 px wide), label under each."""
    tiles = [Image.open(out_dir / f"layout_{k}@2x.png").crop(tuple(v * 2 for v in SHEET_CROP)) for k in keys]
    tw, th = tiles[0].size
    label_h, pad = 70, 20
    sheet = Image.new("RGB", (len(tiles) * (tw + pad) + pad, th + label_h + 2 * pad), (18, 18, 18))
    font = ImageFont.truetype(str(r.te.FONT_PATH), 40)
    draw = ImageDraw.Draw(sheet)
    for i, (img, label) in enumerate(zip(tiles, labels)):
        x = pad + i * (tw + pad)
        sheet.paste(img, (x, pad))
        draw.text((x + 10, pad + th + 12), label, font=font, fill=(240, 216, 144))
    path = out_dir / name
    sheet.save(path)
    return path


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tree", type=Path, default=Path("D:/TAKEALOOK/overwrite/SKSE/Plugins/SpellLearning/spell_tree.json"))
    ap.add_argument("--video3", type=Path, default=Path("C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video3"))
    ap.add_argument("--out", type=Path, default=Path("C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video3/render/layouts"))
    ap.add_argument("--row-size", type=float, default=r.LIST_ROW_FALLBACK)
    ap.add_argument("--ss", type=int, default=4)
    args = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(name)s: %(message)s")
    poison = args.video3 / "expected/base_v3_poison.png"
    heal = args.video3 / "frames/f_014.jpg"
    for path in (args.tree, poison, heal):
        if not path.exists():
            log.error("input missing: %s", path)
            return 2
    ctx = r.Ctx(min(max(args.row_size, r.LIST_ROW_MIN), r.LIST_ROW_MAX))
    world = r.World(mm.load_tree(args.tree), r.KNOWN_SPELLS, {"Novice": 100.0})
    heal_frame, card_base = r.load_frame(heal), r.load_frame(poison)
    report: list[str] = []
    for layout in LAYOUTS:
        for line in render_layout(layout, world, poison, heal_frame, card_base, ctx, args.ss, args.out):
            log.info("%s", line)
            report.append(line)
    for line in render_layout(LAYOUT_E, world, poison, heal_frame, card_base, ctx, args.ss, args.out):
        log.info("%s", line)
        report.append(line)
    for line in render_layout(LAYOUT_E2, world, poison, heal_frame, card_base, ctx, args.ss, args.out):
        log.info("%s", line)
        report.append(line)
    report += ["E2 guides: " + g for g in guides(LAYOUT_E2, ctx, args.out)]
    report.append("sheet " + str(compare_row(args.out, ["E", "E2"], ["E. C+D", "E2. E 오와 열 맞춤"], "compare_E_E2.png")))
    report.append(f"sheet {compare_sheet(args.out)}")
    report.append("sheet " + str(compare_row(args.out, ["A", "C", "E"], ["A. 현재 배치", "C. 설명칸 크게", "E. C+D"], "compare_CD.png")))
    (args.out / "layouts_log.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
