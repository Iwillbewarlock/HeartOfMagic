"""Build 7 mockups (spec docs/superpowers/specs/2026-10-25-magic-menu-proto-v7.md): our own info card (C1), the
Powers/Shouts and Active Effects tabs in our look (C2), the preview without its backdrop disc (P7).

    python build7.py [--out DIR]

Left halves are real build-6 (video6) or build-3 (video3) frames; the right half is drawn here. The card follows
Widget.cpp RedrawOwnCard / ViewOf as of 2026-10-25 (provisional: the UI agent is still editing it).
"""
from __future__ import annotations

import argparse
import logging
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import layouts as L
import layouts_v6 as V
import menu_model as mm
import render_menu as r
import textengine as te
from textengine import Style

log = logging.getLogger("build7")

VIDEO6 = Path("C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video6/frames")
VIDEO3 = Path("C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video3")

# Widget.cpp kStrings, terminology pass 2026-10-25 (spec v7 "Strings")
STRINGS_V7 = {
    "progress_target": "학습 목표: {spell} ({pct}%)",
    "progress_none_candidates": "학습 목표 없음 · 배울 수 있는 주문 {n}개 · {key} 다음 주문",
    "progress_none_nothing": "배울 수 있는 주문 없음 (선행 조건 미충족)",
    "node_set_target": "학습 목표 지정 {key}",
    "node_is_target": "현재 학습 목표",
    "node_target_pct": "현재 학습 목표 · {pct}%",
    "node_locked": "선행 조건 필요",
    "req_all": "선행 조건 모두 숙달 필요",
    "req_some": "선행 조건 중 {n}개 숙달 필요",
    "req_none": "선행 조건 없음",
    "xp": "{cur} / {need} 경험치",
}

# Widget.cpp (build 7, provisional): RedrawOwnCard / OwnCardRect
CARD_RULE_GAP = 5.0        # kCardRuleGap
CARD_FOOT_SHARE = 0.62     # kCardFootShare
SPELL_CARD = (648, 417, 969, 623)    # spell tabs: the item card slot
POWER_CARD = (648, 306, 969, 623)    # Powers/Shouts: preview.bottom + panelGap .. itemCard.bottom
EFFECT_CARD = (648, 67, 969, 623)    # Active Effects: preview.top .. itemCard.bottom, cut to content
GROW_CAP = 3.0             # PreviewMath.h kGrowMost, used here against build 6's on-screen size (estimate)


@dataclass
class Run:
    text: str
    color: int


@dataclass
class CardView:
    name: str
    sub: str = ""
    words: list[Run] = field(default_factory=list)
    effects: str = ""
    bright: list[str] = field(default_factory=list)   # spans of the description the game's markup colours (magnitudes)
    foot_left: str = ""
    foot_right: str = ""


def draw_runs(c: r.Canvas, x: float, y: float, w: float, size: float, runs: list[Run]) -> float:
    """One line of coloured runs (the shout words' HTML line), cut at the field width."""
    pen = x + te.GUTTER
    baseline = y + te.GUTTER + te.BASELINE * size
    for run in runs:
        c.text_line(pen, baseline, run.text, Style(size), run.color, clip=(x, y, x + w, y + r.field_h(size)))
        pen += te.width(run.text, Style(size))
    return r.field_h(size)


def draw_desc(c: r.Canvas, x: float, y: float, w: float, max_h: float, size: float, text: str, bright: list[str]) -> float:
    """The description at the reading size: wrapped, as many lines as fit, "..." on the last; spans in `bright` white."""
    style = Style(size)
    lines_fit = max(1, int((max_h - r.GUTTER) // (te.LINE_PITCH * size)))
    final = r.fit_text(w, r.field_h(size, lines_fit), text, lambda p: p, size, True)
    _, _, lines = te.text_size(final, style, w, True)
    for i, line in enumerate(lines[:lines_fit]):
        baseline = y + te.GUTTER + te.BASELINE * size + i * te.LINE_PITCH * size
        c.text_line(x + te.GUTTER, baseline, line, style, r.LABEL_COLOR)
        for span in bright:
            at = line.find(span)
            while at >= 0:
                c.text_line(x + te.GUTTER + te.width(line[:at], style), baseline, span, style, r.WHITE)
                at = line.find(span, at + len(span))
    return min(len(lines), lines_fit) * te.LINE_PITCH * size + r.GUTTER


def c1_card(c: r.Canvas, ctx: r.Ctx, rect: tuple, view: CardView, fit: bool, out: list[str]) -> float:
    """Widget.cpp RedrawOwnCard: name (19, 2 lines), sub line, shout words, rule, description, footer (rule, cost left
    in 62% of the width, cast type right). With `fit` the panel is cut to its content (Active Effects)."""
    w, max_h = rect[2] - rect[0] - 2 * r.PAD, rect[3] - rect[1]
    sub, body = ctx.size(r.SUB_SIZE), ctx.size(r.ROW_SIZE)
    foot = bool(view.foot_left or view.foot_right)
    foot_h = r.field_h(sub)
    reserve = 2 * CARD_RULE_GAP + foot_h + r.PAD if foot else r.PAD
    # measure first (the panel fill goes under the text)
    layer = Image.new("RGB", c.img.size)
    lc = r.Canvas(layer, c.ss)
    with lc.at(rect[0] + r.PAD, rect[1]):
        y = r.CARD_TOP
        _, nh = r.fit_lines(lc, 0, y, w, r.NAME_SIZE, r.NAME_LINES, view.name, r.WHITE)
        y += min(max(nh, r.field_h(r.NAME_SIZE)), r.field_h(r.NAME_SIZE, r.NAME_LINES))
        if view.sub:
            r.fit_line(lc, 0, y, w, sub, view.sub, r.TITLE_COLOR)
            y += r.field_h(sub)
        if view.words:
            y += draw_runs(lc, 0, y, w, sub, view.words)
        y += CARD_RULE_GAP
        lc.polyline([(0, y), (w, y)], r.EDGE_W, r.PANEL_EDGE)
        y += CARD_RULE_GAP
        used = y
        if view.effects and max_h - reserve - y >= r.field_h(8.0):
            used = y + draw_desc(lc, 0, y, w, max_h - reserve - y, body, view.effects, view.bright)
        h = min(max_h, used + reserve) if fit else max_h
        if foot:
            rule_y = h - reserve + CARD_RULE_GAP
            lc.polyline([(0, rule_y), (w, rule_y)], r.EDGE_W, r.PANEL_EDGE)
            foot_y = h - r.PAD - foot_h
            if view.foot_left:
                r.fit_line(lc, 0, foot_y, w * CARD_FOOT_SHARE, sub, view.foot_left, r.LABEL_COLOR)
            if view.foot_right:
                r.fit_line(lc, w * CARD_FOOT_SHARE, foot_y, w * (1 - CARD_FOOT_SHARE), sub, view.foot_right, r.DIM_TEXT, "right")
    c.rect_fill(rect[0], rect[1], rect[2] - rect[0], h, r.PANEL_FILL, r.PANEL_ALPHA)
    mask = layer.convert("L").point(lambda v: 255 if v > 0 else 0)
    c.img.paste(layer, (0, 0), mask)
    out.append(f"C1 card {rect} h {h:.1f}: {view.name} | {view.sub} | {view.foot_left} | {view.foot_right}")
    return h


# ------------------------------------------------------------------------------------------------------------
# Preview without the disc (P7.1), sized as the fit would be without it (estimate)
# ------------------------------------------------------------------------------------------------------------

def effect_no_disc(frame: Image.Image) -> tuple[np.ndarray, float]:
    """The build-6 model from the E2 preview content area, the Cylinder08 disc masked out (luminance below 28 black,
    full from 58), and the scale an unclipped bound-sphere fit would reach: the visible effect's extent (around its
    bright core) filling kFill of min(width, height), capped at kGrowMost x build 6's size."""
    a = V.poison_effect(frame, True)
    lum = a.mean(axis=2)
    core = lum > 200
    cy, cx = (np.nonzero(core)[0].mean(), np.nonzero(core)[1].mean()) if core.any() else (a.shape[0] / 2, a.shape[1] / 2)
    ys, xs = np.nonzero(lum > 40)
    extent = 2 * max(cx - xs.min(), xs.max() - cx, cy - ys.min(), ys.max() - cy) + 2
    l, t, rr, b = V.content(V.E2["preview"])
    grow = min(r.PREVIEW_FILL * min(rr - l, b - t) / extent, GROW_CAP)
    return a, grow


def place_effect(canvas: Image.Image, ss: float, effect: np.ndarray, grow: float, preview: tuple) -> str:
    l, t, rr, b = V.content(preview)
    img = Image.fromarray(effect.clip(0, 255).astype(np.uint8))
    w, h = img.width * grow, img.height * grow
    big = img.resize((round(w * ss), round(h * ss)), Image.LANCZOS)
    # the model's core stays where build 6 had it relative to the content centre (it was centred there)
    cx, cy = (l + rr) / 2, (t + b) / 2
    px, py = round((cx - w / 2) * ss), round((cy - h / 2 + 40) * ss)
    cl, ct, cr, cb = round(l * ss), round((t + 40) * ss), round(rr * ss), round((b + 40) * ss)
    sx0, sy0 = max(0, cl - px), max(0, ct - py)
    sx1, sy1 = min(big.width, cr - px), min(big.height, cb - py)
    region = np.asarray(big.crop((sx0, sy0, sx1, sy1))).astype(int)
    base = np.asarray(canvas.crop((px + sx0, py + sy0, px + sx1, py + sy1))).astype(int)
    canvas.paste(Image.fromarray(np.minimum(255, base + region).astype(np.uint8)), (px + sx0, py + sy0))
    return f"preview: disc masked, x {grow:.2f} of build 6's size (estimate)"


# ------------------------------------------------------------------------------------------------------------
# Scenes
# ------------------------------------------------------------------------------------------------------------

POISON_VIEW = CardView("치명적인 독 분사", "회복마법 · 숙련자", effects=L.POISON_CARD["description"], bright=["20"],
                       foot_left="매지카 52", foot_right="집중")
# video6 f_063 (the real SkyUI card text); its spell type is not readable offline -> 하급 특수능력 ASSUMED (a toggle)
POWER_VIEW = CardView("독물학", "하급 특수능력",
                      effects="독물학을 켜거나 끈다. 켜 두면 독 주문에 즐겨찾기 맨 위에 있는 독의 효과가 함께 실리고, "
                              "꺼 두면 독이 실리지 않으며 병도 줄지 않는다.")
# Skyrim.esm SHOU 0x00013E07 (FULL, DESC, SNAM recovery 15/20/45) and its WOOP words (FULL dragon word, TNAM meaning),
# strings from 스카이림 한국어 패치 Skyrim_english.(DL)STRINGS. Which words the player knows: ASSUMED (first only).
SHOUT_VIEW = CardView("거침없는 힘", "함성",
                      words=[Run("FUS 힘", r.WHITE), Run("  ·  ", r.LABEL_COLOR), Run("RO 균형", r.DIM_TEXT),
                             Run("  ·  ", r.LABEL_COLOR), Run("D4 밀기", r.DIM_TEXT)],
                      effects="그대의 목소리는 힘 자체이며, 그대의 앞을 가로막는 것이 무엇이든, 혹은 누구든 간에 밀어버릴 것이다.",
                      foot_left="충전 15 / 20 / 45초")
# video6 f_105: 평판: 알 수 없음 (the card's name 알 수 없음, no time left "-")
EFFECT_VIEW = CardView("알 수 없음", "적용중인 효과",
                       effects="당신은 이 지역에서 아직 알려지지 않았으며, 당신에 대한 소문은 이제 막 시작되고 있습니다.")


@dataclass
class Scene:
    key: str
    label: str
    frame: Path
    masks: list[tuple]          # old panels in the 1280x800 frame, filled from around them
    tab: str                    # "spell", "power", "effects"
    effect_frame: Path | None
    view: CardView
    notes: list[str]
    left_note: str = ""


SCENES = [
    Scene("M1", "주문 탭", VIDEO3 / "expected/base_v3_poison.png", r.OLD_PANELS, "spell", VIDEO6 / "f_029.jpg", POISON_VIEW,
          ["미리보기: 빌드 6 실제 독 분사 모델(video6 f_029), 원반 지움, 원반 없는 맞춤 크기는 추정"]),
    Scene("M2", "특수능력 탭", VIDEO6 / "f_063.jpg", [(645, 104, 972, 340), (646, 350, 1078, 612)], "power", VIDEO6 / "f_063.jpg",
          POWER_VIEW, ["미리보기: 빌드 6 실제 독물학 모델(f_063), 원반 지움, 크기 추정 · '하급 특수능력'은 가정(주문 종류 확인 불가)"]),
    Scene("M3", "함성 탭", VIDEO6 / "f_063.jpg", [(645, 104, 972, 340), (646, 350, 1078, 612)], "power", None, SHOUT_VIEW,
          ["카드 값: Skyrim.esm + 한국어 패치 문자열 (충전 15/20/45초는 바닐라 값, 모드 덮어쓰기 미확인) · 아는 단어(첫 번째만)는 가정",
           "미리보기: 함성 3D 화면 캡처 없음 → 빈 상자로 둠 · 'D4'는 한국어 패치의 실제 단어 문자열 그대로"],
          left_note="왼쪽 목록: 특수능력 탭 프레임 (함성 탭 캡처 없음)"),
    Scene("M4", "적용중인 효과 탭", VIDEO6 / "f_105.jpg", [(646, 460, 1078, 724), (760, 180, 860, 290)], "effects", None, EFFECT_VIEW,
          ["미리보기 상자 없음, 떠다니는 3D 없음 (P7.2) · 카드 내용: f_105 실제 카드 (평판: 알 수 없음, 남은 시간 없음)"]),
]


def render_scene(s: Scene, world: r.World, ctx: r.Ctx, ss: int, out_dir: Path) -> list[str]:
    report = [f"== {s.key} {s.label}"]
    r.OLD_PANELS = s.masks
    big = r.repaint_right(r.load_frame(s.frame)).resize((1280 * ss, 800 * ss), Image.LANCZOS)
    c = r.Canvas(big, ss)
    V.apply(V.E2)
    if s.tab == "spell":
        tree = world.tree
        school = "Restoration"
        target = tree.nodes[r.node_index(tree, "회춘의 손길", school)]
        snap = mm.build_snapshot(tree, mm.PlayerState(world.known_ids(), {school: (target.form_id, 40.0)}, world.required_xp), [school])
        centre = r.node_index(tree, "치명적인 독 분사", school)
        for rect, title, alpha in ((r.PREVIEW, r.S["preview_title"], r.PREVIEW_ALPHA), (r.TREE, r.S["tree_title"], r.PANEL_ALPHA),
                                   (r.PROGRESS, None, r.PANEL_ALPHA), (r.NODE_CARD, None, r.PANEL_ALPHA)):
            r.panel(c, rect, title, alpha)
        r.draw_legend(c, ctx)
        effect, grow = effect_no_disc(r.load_frame(s.effect_frame))
        report.append(place_effect(big, ss, effect, grow, r.PREVIEW))
        V.tree_b6(c, ctx, tree, snap, centre, report)
        report += r.redraw_tree(c, ctx, tree, snap, centre, -1).lines
        V.progress_b6(c, ctx, tree, snap, school, r.NODE_CARD[1], report)
        saved = {k: getattr(r, k) for k in V.NODE_BOTTOM}
        for k, val in V.NODE_BOTTOM.items():
            setattr(r, k, val)
        try:
            r.redraw_node_card(c, ctx, tree, snap, tree.nodes.index(target), report)
        finally:
            for k, val in saved.items():
                setattr(r, k, val)
        c1_card(c, ctx, SPELL_CARD, s.view, False, report)
    elif s.tab == "power":
        r.panel(c, r.PREVIEW, r.S["preview_title"], r.PREVIEW_ALPHA)
        if s.effect_frame:
            effect, grow = effect_no_disc(r.load_frame(s.effect_frame))
            report.append(place_effect(big, ss, effect, grow, r.PREVIEW))
        c1_card(c, ctx, POWER_CARD, s.view, False, report)
    else:
        c1_card(c, ctx, EFFECT_CARD, s.view, True, report)
    L.label_strip(c, f"빌드 7 목업 · {s.label}")
    if s.left_note:
        st = Style(13.0)
        w = te.width(s.left_note, st) + 12
        c.rect_fill(20, 500, w, 22, 0x000000, 80)
        c.polyline([(20, 500), (20 + w, 500), (20 + w, 522), (20, 522), (20, 500)], 1, 0xC0A040)
        c.text_line(26, 516, s.left_note, st, 0xF0D890)
    r.annotate(big, ss, [f"렌더 주석 (게임 화면 아님) · 빌드 7 목업 · {s.label} · 카드 배치는 Widget.cpp 진행 중 값 (잠정)"] + s.notes)
    out_dir.mkdir(parents=True, exist_ok=True)
    name = f"build7_{s.key}"
    big.resize((2560, 1600), Image.LANCZOS).save(out_dir / f"{name}@2x.png")
    big.resize((1280, 800), Image.LANCZOS).save(out_dir / f"{name}.png")
    report.append(f"saved {out_dir / (name + '.png')} and @2x")
    return report


def compare(out_dir: Path) -> Path:
    tiles = [Image.open(out_dir / f"build7_{s.key}@2x.png").crop(tuple(v * 2 for v in L.SHEET_CROP)) for s in SCENES]
    tw, th = tiles[0].size
    label_h, pad = 70, 20
    sheet = Image.new("RGB", (2 * tw + 3 * pad, 2 * (th + label_h) + 3 * pad), (18, 18, 18))
    font = ImageFont.truetype(str(te.FONT_PATH), 40)
    d = ImageDraw.Draw(sheet)
    for i, (img, s) in enumerate(zip(tiles, SCENES)):
        x, y = pad + (i % 2) * (tw + pad), pad + (i // 2) * (th + label_h + pad)
        sheet.paste(img, (x, y))
        d.text((x + 10, y + th + 12), s.label, font=font, fill=(240, 216, 144))
    path = out_dir / "compare_build7.png"
    sheet.save(path)
    return path


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tree", type=Path, default=Path("D:/TAKEALOOK/overwrite/SKSE/Plugins/SpellLearning/spell_tree.json"))
    ap.add_argument("--out", type=Path, default=VIDEO3 / "render/build7")
    ap.add_argument("--row-size", type=float, default=r.LIST_ROW_FALLBACK)
    ap.add_argument("--ss", type=int, default=4)
    args = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(name)s: %(message)s")
    for s in SCENES:
        for p in (s.frame, s.effect_frame):
            if p and not p.exists():
                log.error("input missing: %s", p)
                return 2
    r.PANEL_BORDER = False
    r.S.update(STRINGS_V7)
    ctx = r.Ctx(min(max(args.row_size, r.LIST_ROW_MIN), r.LIST_ROW_MAX))
    world = r.World(mm.load_tree(args.tree), r.KNOWN_SPELLS, V.REQUIRED_XP)
    report: list[str] = []
    for s in SCENES:
        for line in render_scene(s, world, ctx, args.ss, args.out):
            log.info("%s", line)
            report.append(line)
    report.append(f"sheet {compare(args.out)}")
    (args.out / "build7_log.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
