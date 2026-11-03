"""Build-9 mockups: the spell tree window now vs a proposal (tree_compare.png), and three ways to hide the black blob
behind the healing art in the preview box (halo_options.png).

    python build9_mock.py [--tree spell_tree.json] [--video9 DIR] [--out DIR]

"지금" panels are the real build-9 frames (video9/tree_*.png). "개선안" panels are drawn here from the real tree data
(Data/SKSE/Plugins/SpellLearning/spell_tree.json) over the same frame with its marks filtered out. PROPOSAL, not plugin code.
"""
from __future__ import annotations

import argparse
import logging
import math
import sys
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import scipy.ndimage as nd
from PIL import Image, ImageDraw, ImageFilter, ImageFont

import layouts_v6 as V
import menu_model as mm
import render_menu as r
import textengine as te
from menu_model import State
from textengine import Style

log = logging.getLogger("build9_mock")

# tree window (Settings E2, build 9): 260 x 230 stage units, drawn at 2x in the video9 crops
WIN_W, WIN_H = 260.0, 230.0
TITLE_H, PAD = r.PANEL_TITLE_H, r.PANEL_PAD
LEGEND_H = 13.0                     # proposal: one smaller line (build 9: 18)
FIT_MARGIN = 12.0                   # from the content box edge to the outermost mark centre
MAX_ZOOM = 1.2                      # stage units per tree unit at most (few nodes)
MIN_FIT_GAP = 15.0                  # stop adding nodes when the closest pair of step-0/1 nodes would fall below this
MIN_GAP = 22.0                      # centre to centre after push-apart
MAX_NODES = 25
EDGE_CLEAR = 6.0                    # a mark keeps this far (plus its radius) from links it is not on
# marks 1.4x build 9 (kNodeR 7, kRingGap 3, kArcWidth 2.5, kLineWidth 1)
NODE_R, RING_GAP, ARC_W, MARK_LINE = 9.8, 3.5, 3.0, 1.5
EDGE_W, PATH_W = 1.6, 2.6
STEP2_FADE = 0.5                    # step-2 colours mixed this far toward the panel
BG = 0x0E0C0A                       # the panel's colour over the blurred frame (measured on the crops)
PATH_COLOR = 0x8CC8FF
HOVER_COLOR = 0xFFE7A0
LABEL1_SIZE, LABEL2_SIZE, CENTRE_SIZE = 11.0, 10.0, 12.0
LABEL_CHARS = 7
LEGEND_SIZE, LEGEND_ICON_R = 8.5, 3.5
BADGE_SIZE = 10.0
ARROWS = ["→", "↘", "↓", "↙", "←", "↖", "↑", "↗"]  # 8 screen sectors, 0 = right, clockwise (y down)
SS = 4

STATE_TEXT = {State.MASTERED: "숙달", State.AVAILABLE: "배울 수 있음", State.LOCKED: "잠김",
              State.TARGET: "학습 목표", State.LEARNING: "학습 중", State.READY: "습득 대기"}


def mix(rgb: int, k: float, to: int = BG) -> int:
    """rgb moved k of the way toward `to`."""
    a, b = r.rgb_t(rgb), r.rgb_t(to)
    c = [round(x + (y - x) * k) for x, y in zip(a, b)]
    return (c[0] << 16) | (c[1] << 8) | c[2]


@dataclass
class Row:
    centre: str
    school: str
    crop: str                 # video9 file of the current look
    hover: str                # a step-1 node shown hovered
    target: str               # the school's learning target (ASSUMED)
    pct: float
    caption: str


ROWS = [
    Row("불씨조각", "Destruction", "tree_28.png", "화염 망령", "물의 손길", 30.0, "불씨조각 · 연결 6개 (붐비는 노드)"),
    Row("치유", "Restoration", "tree_70.png", "천체 가루", "회춘의 손길", 40.0, "치유"),
    Row("불꽃의 벽", "Destruction", "tree_10.png", "용암 접촉", "물의 손길", 30.0, "불꽃의 벽 · 연결 적음"),
]


@dataclass
class N:
    index: int
    step: int
    parent: int               # node index of the BFS parent (-1 for the centre)
    rx: float                 # rotated tree offset from the centre (tree units)
    ry: float
    x: float = 0.0            # window position (stage units, window-local)
    y: float = 0.0
    state: State = State.UNKNOWN
    pct: float = 0.0


@dataclass
class Box:
    l: float
    t: float
    r: float
    b: float

    def hits(self, o: "Box") -> bool:
        return self.l < o.r and o.l < self.r and self.t < o.b and o.t < self.b

    def inside(self, o: "Box") -> bool:
        return self.l >= o.l and self.r <= o.r and self.t >= o.t and self.b <= o.b




MAX_TURN_DEG = 25.0      # a step-2 node the push would turn further than this around the centre is left out (badge)
EDGE_NUDGES = 12         # at most this many units of push per node for clearing a link it is not on


@dataclass
class Plan:
    tree: mm.Tree
    centre: int
    rot: float
    nodes: dict[int, N]
    order: list[int]
    edges: list[tuple[int, int, bool]] = field(default_factory=list)   # (a, b, lock), both shown
    hidden: dict[int, tuple[float, float]] = field(default_factory=dict)  # linked, not shown -> rotated offset
    path: list[int] = field(default_factory=list)
    scale: float = 0.0
    left_out: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)


def content_box() -> Box:
    return Box(PAD, TITLE_H + PAD, WIN_W - PAD, WIN_H - LEGEND_H - PAD / 2)


def bfs(tree: mm.Tree, start: int, limit: int) -> dict[int, tuple[int, int]]:
    """node -> (steps, parent) up to `limit` steps."""
    seen = {start: (0, -1)}
    q = deque([start])
    while q:
        cur = q.popleft()
        if seen[cur][0] >= limit:
            continue
        for e in tree.adjacent[cur]:
            ed = tree.edges[e]
            o = ed.b if ed.a == cur else ed.a
            if o not in seen:
                seen[o] = (seen[cur][0] + 1, cur)
                q.append(o)
    return seen


def path_to(tree: mm.Tree, start: int, goal: int) -> list[int]:
    seen = bfs(tree, start, 99)
    if goal not in seen:
        return []
    out = [goal]
    while out[-1] != start:
        out.append(seen[out[-1]][1])
    return out[::-1]


def rotated(tree: mm.Tree, centre: int, index: int, rot: float) -> tuple[float, float]:
    c, n = tree.nodes[centre], tree.nodes[index]
    dx, dy = n.x - c.x, n.y - c.y
    a = math.radians(rot)
    return dx * math.cos(a) - dy * math.sin(a), dx * math.sin(a) + dy * math.cos(a)


def fit(nodes: list[N]) -> tuple[float, float, float]:
    """Scale and offset placing the nodes' bounding box in the content box (uniform, directions kept)."""
    cb = content_box()
    xs, ys = [n.rx for n in nodes], [n.ry for n in nodes]
    bw, bh = max(xs) - min(xs), max(ys) - min(ys)
    room_w, room_h = cb.r - cb.l - 2 * FIT_MARGIN, cb.b - cb.t - 2 * FIT_MARGIN
    s = min(room_w / bw if bw > 1e-6 else MAX_ZOOM, room_h / bh if bh > 1e-6 else MAX_ZOOM, MAX_ZOOM)
    ox = (cb.l + cb.r) / 2 - s * (min(xs) + max(xs)) / 2
    oy = (cb.t + cb.b) / 2 - s * (min(ys) + max(ys)) / 2
    return s, ox, oy


def min_pair(nodes: list[N], s: float) -> float:
    pts = [(n.rx * s, n.ry * s) for n in nodes if n.step <= 1]
    return min((math.dist(a, b) for i, a in enumerate(pts) for b in pts[i + 1:]), default=1e9)


def seg_dist(px: float, py: float, ax: float, ay: float, bx: float, by: float) -> float:
    vx, vy = bx - ax, by - ay
    L2 = vx * vx + vy * vy
    t = 0.0 if L2 < 1e-9 else max(0.0, min(1.0, ((px - ax) * vx + (py - ay) * vy) / L2))
    return math.hypot(px - ax - t * vx, py - ay - t * vy)


def reach(n: N) -> float:
    return NODE_R + (RING_GAP + ARC_W if n.state in (State.TARGET, State.LEARNING, State.READY) else MARK_LINE)


def need(a: N, b: N) -> float:
    return max(MIN_GAP, reach(a) + reach(b) + 1.0)


def turn_deg(plan: Plan, n: N) -> float:
    c = plan.nodes[plan.centre]
    a0 = math.degrees(math.atan2(n.ry, n.rx))
    a1 = math.degrees(math.atan2(n.y - c.y, n.x - c.x))
    return (a1 - a0 + 180) % 360 - 180


def refresh_links(plan: Plan) -> None:
    tree = plan.tree
    plan.edges, taken, plan.hidden = [], set(), {}
    for i in plan.order:
        for e in tree.adjacent[i]:
            ed = tree.edges[e]
            o = ed.b if ed.a == i else ed.a
            if o in plan.nodes:
                if e not in taken and ed.a != ed.b:
                    taken.add(e)
                    plan.edges.append((ed.a, ed.b, ed.lock))
            elif o not in plan.hidden and tree.nodes[o].has_xy:
                plan.hidden[o] = rotated(tree, plan.centre, o, plan.rot)


def drop(plan: Plan, index: int, why: str) -> None:
    plan.order.remove(index)
    del plan.nodes[index]
    plan.left_out.append(f"{plan.tree.nodes[index].name} ({why})")


def plan_tree(tree: mm.Tree, snap: mm.Snapshot, centre: int, school: str, target: int) -> Plan:
    rot = mm.school_rotation_deg(tree, mm.school_named(tree, school))
    cands = []
    for i, (step, parent) in bfs(tree, centre, 2).items():
        if not tree.nodes[i].has_xy:
            continue
        rx, ry = rotated(tree, centre, i, rot)
        st, pct = mm.look_of(tree, snap, i)
        cands.append(N(i, step, parent, rx, ry, state=st, pct=pct))
    cands.sort(key=lambda n: (n.step, math.hypot(n.rx, n.ry), n.index))
    shown = [cands[0]]
    notes = []
    for n in cands[1:]:
        if len(shown) >= MAX_NODES:
            notes.append(f"cap {MAX_NODES} reached")
            break
        if n.parent not in {m.index for m in shown}:
            continue                      # its step-1 link was cut: it would float
        trial = shown + [n]
        s, _, _ = fit(trial)
        if len(shown) > 1 and min_pair(trial, s) < MIN_FIT_GAP:
            notes.append(f"stop before {tree.nodes[n.index].name} (step {n.step}, {math.hypot(n.rx, n.ry):.0f} tree units): "
                         f"closest step-0/1 pair would be {min_pair(trial, s):.1f} units")
            break
        shown.append(n)
    plan = Plan(tree, centre, rot, {n.index: n for n in shown}, [n.index for n in shown], notes=notes)
    plan.path = path_to(tree, centre, target)
    for _ in range(10):                   # fit, push, leave out what turned too far, again
        s, ox, oy = fit([plan.nodes[i] for i in plan.order])
        plan.scale = s
        for n in plan.nodes.values():
            n.x, n.y = ox + n.rx * s, oy + n.ry * s
        refresh_links(plan)
        push_apart(plan)
        turned = [i for i in plan.order[1:] if plan.nodes[i].step == 2 and abs(turn_deg(plan, plan.nodes[i])) > MAX_TURN_DEG]
        if turned:
            worst = max(turned, key=lambda i: abs(turn_deg(plan, plan.nodes[i])))
            drop(plan, worst, f"push would turn it {turn_deg(plan, plan.nodes[worst]):+.0f} deg")
            continue
        clash = [b for ai, a in enumerate(plan.order) for b in plan.order[ai + 1:]
                 if math.dist((plan.nodes[a].x, plan.nodes[a].y), (plan.nodes[b].x, plan.nodes[b].y)) < reach(plan.nodes[a]) + reach(plan.nodes[b])
                 and plan.nodes[b].step == 2]
        if not clash:
            break
        drop(plan, clash[-1], "its mark could not be pushed clear")
    refresh_links(plan)
    return plan


def children(plan: Plan, index: int) -> list[int]:
    return [i for i, n in plan.nodes.items() if n.parent == index]


def push_apart(plan: Plan) -> None:
    """Marks closer than need() move outward along their own link from their BFS parent (the one farther from the centre
    moves), carrying their children; a mark on a link it is not on gets up to EDGE_NUDGES units of the same push. If a mark
    leaves the window, the picture shrinks 3% toward the window's middle and the push is redone."""
    cb = content_box()
    inner = Box(cb.l + NODE_R - PAD / 2, cb.t + NODE_R - PAD / 2, cb.r - NODE_R + PAD / 2, cb.b - NODE_R + PAD / 2)
    base = {i: (n.x, n.y) for i, n in plan.nodes.items()}
    cx0, cy0 = (cb.l + cb.r) / 2, (cb.t + cb.b) / 2   # shrink toward the window's middle (the centre node may sit at an edge)
    for attempt in range(25):
        shrink = 0.97 ** attempt
        for i, n in plan.nodes.items():
            n.x, n.y = cx0 + (base[i][0] - cx0) * shrink, cy0 + (base[i][1] - cy0) * shrink
        edge_used = {i: 0 for i in plan.nodes}
        for _ in range(300):
            moved = False
            for ai, a in enumerate(plan.order):
                na = plan.nodes[a]
                for b in plan.order[ai + 1:]:
                    nb = plan.nodes[b]
                    if math.dist((na.x, na.y), (nb.x, nb.y)) >= need(na, nb):
                        continue
                    mover = pick_mover(plan, na, nb)
                    if mover is None:
                        continue          # neither push separates them: left for plan_tree to resolve
                    nudge(plan, mover, 1.0)
                    moved = True
                if na.step == 0 or edge_used[a] >= EDGE_NUDGES:
                    continue
                for ea, eb, _ in plan.edges:
                    if a in (ea, eb):
                        continue
                    p, q = plan.nodes[ea], plan.nodes[eb]
                    if seg_dist(na.x, na.y, p.x, p.y, q.x, q.y) < NODE_R + EDGE_CLEAR:
                        nudge(plan, na, 1.0)
                        edge_used[a] += 1
                        moved = True
                        break
            if not moved:
                break
        if all(Box(n.x, n.y, n.x, n.y).inside(inner) for n in plan.nodes.values()):
            if attempt:
                plan.notes.append(f"push-apart: shrank to {shrink:.2f}x of the fit to keep every mark inside")
            return
    plan.notes.append("push-apart: some mark stays outside after 25 shrinks")


def push_dir(plan: Plan, n: N) -> tuple[float, float]:
    if n.parent < 0:
        return 0.0, 0.0
    p = plan.nodes[n.parent]
    ux, uy = n.rx - p.rx, n.ry - p.ry
    L = math.hypot(ux, uy)
    return (ux / L, uy / L) if L > 1e-9 else (0.0, 0.0)


def is_ancestor(plan: Plan, a: int, b: int) -> bool:
    i = plan.nodes[b].parent
    while i >= 0:
        if i == a:
            return True
        i = plan.nodes[i].parent if i in plan.nodes else -1
    return False


def pick_mover(plan: Plan, na: N, nb: N) -> N | None:
    """The node whose push along its own link separates the pair, the outer one (higher step, farther) when both do."""
    best = None
    for m, o in ((na, nb), (nb, na)):
        if m.step == 0 or is_ancestor(plan, m.index, o.index):
            continue
        ux, uy = push_dir(plan, m)
        dx, dy = m.x - o.x, m.y - o.y
        L = math.hypot(dx, dy)
        gain = (ux * dx + uy * dy) / L if L > 1e-9 else 1.0
        if gain <= 0.15:
            continue
        key = (m.step, math.hypot(m.rx, m.ry))
        if best is None or key > best[0]:
            best = (key, m)
    return best[1] if best else None


def nudge(plan: Plan, n: N, d: float) -> None:
    if n.parent < 0:
        return
    p = plan.nodes[n.parent]
    ux, uy = n.rx - p.rx, n.ry - p.ry
    L = math.hypot(ux, uy)
    if L < 1e-9:
        return
    ux, uy = ux / L, uy / L
    stack = [n.index]
    while stack:
        i = stack.pop()
        m = plan.nodes[i]
        m.x += ux * d
        m.y += uy * d
        stack += children(plan, i)


# ------------------------------------------------------------------------------------------------------------
# Label, badge and tooltip layout (computed before anything is drawn)
# ------------------------------------------------------------------------------------------------------------

def mark_box(n: N) -> Box:
    k = reach(n)
    return Box(n.x - k, n.y - k, n.x + k, n.y + k)


def trimmed(p: N, q: N) -> tuple[tuple, tuple]:
    L = math.dist((p.x, p.y), (q.x, q.y))
    if L < 2 * NODE_R + 1:
        return (p.x, p.y), (p.x, p.y)
    ux, uy = (q.x - p.x) / L, (q.y - p.y) / L
    k = NODE_R + 1.0
    return (p.x + ux * k, p.y + uy * k), (q.x - ux * k, q.y - uy * k)


def short_name(name: str) -> str:
    base = name
    while "(" in base and ")" in base[base.index("("):]:
        i = base.index("(")
        j = base.index(")", i)
        base = (base[:i] + base[j + 1:]).replace("  ", " ")
    base = base.strip()
    return base if len(base) <= LABEL_CHARS + 1 else base[:LABEL_CHARS].rstrip() + "…"


def text_box_size(text: str, size: float) -> tuple[float, float]:
    return te.width(text, Style(size)) + 2 * te.GUTTER, size * te.LINE_PITCH + te.GUTTER


def link_boxes(plan: Plan) -> list[Box]:
    out = []
    for a, b, _ in plan.edges:
        (ax, ay), (bx, by) = trimmed(plan.nodes[a], plan.nodes[b])
        n = max(1, int(math.dist((ax, ay), (bx, by)) / 3))
        out += [Box(ax + (bx - ax) * i / n - 1, ay + (by - ay) * i / n - 1, ax + (bx - ax) * i / n + 1, ay + (by - ay) * i / n + 1)
                for i in range(n + 1)]
    return out


def label_places(n: N, w: float, h: float, centre: N) -> list[Box]:
    """Candidate label boxes around a mark: 8 sides at growing distance, the side facing away from the centre first."""
    g0 = reach(n) + 1.5
    ox, oy = n.x - centre.x, n.y - centre.y
    L = math.hypot(ox, oy)
    ox, oy = (ox / L, oy / L) if L > 1e-6 else (1.0, 0.0)
    out = []
    for extra in (0.0, 4.0, 9.0, 15.0):
        g = g0 + extra
        cand = []
        for dx, dy in ((1, 0), (-1, 0), (0, -1), (0, 1), (1, -1), (1, 1), (-1, -1), (-1, 1)):
            for slide in ((0.0,) if dy else (0.0, -h / 2, h / 2)):
                if dx and not dy:
                    b = Box(n.x + g, n.y - h / 2 + slide, n.x + g + w, n.y + h / 2 + slide) if dx > 0 else \
                        Box(n.x - g - w, n.y - h / 2 + slide, n.x - g, n.y + h / 2 + slide)
                elif dy and not dx:
                    b = Box(n.x - w / 2, n.y - g - h + 1, n.x + w / 2, n.y - g + 1) if dy < 0 else \
                        Box(n.x - w / 2, n.y + g - 1, n.x + w / 2, n.y + g + h - 1)
                else:
                    k = g * 0.62
                    bx = n.x + k if dx > 0 else n.x - k - w
                    by = n.y + k if dy > 0 else n.y - k - h
                    b = Box(bx, by, bx + w, by + h)
                cand.append((-(dx * ox + dy * oy) / math.hypot(dx, dy) + abs(slide) * 0.02, b))
        out += [b for _, b in sorted(cand, key=lambda t: t[0])]
    return out


@dataclass
class Label:
    index: int
    text: str
    size: float
    box: Box
    over_link: bool = False      # placed across a link: drawn on a dark backing


@dataclass
class Badge:
    text: str
    box: Box
    lead: bool


@dataclass
class Layout:
    labels: list[Label] = field(default_factory=list)
    badges: list[Badge] = field(default_factory=list)
    dropped: list[str] = field(default_factory=list)


def label_text(plan: Plan, i: int) -> str:
    n = plan.nodes[i]
    name = plan.tree.nodes[i].name if n.step == 0 else short_name(plan.tree.nodes[i].name)
    return name + (f" {n.pct:.0f}%" if n.state == State.TARGET else "")


def place_labels(plan: Plan, which: list[int], blockers: list[Box], links: list[Box], out: Layout) -> None:
    window = Box(2.0, TITLE_H, WIN_W - 2.0, WIN_H - LEGEND_H - 1)
    centre = plan.nodes[plan.centre]
    for i in which:
        n = plan.nodes[i]
        text = label_text(plan, i)
        size = CENTRE_SIZE if n.step == 0 else LABEL1_SIZE if n.step == 1 else LABEL2_SIZE
        sizes = (size, size - 1, size - 2) if n.step <= 1 else (size,)
        placed = None
        for strict in (True, False):
            for sz in sizes:
                w, h = text_box_size(text, sz)
                for b in label_places(n, w, h, centre):
                    if b.inside(window) and not any(b.hits(o) for o in blockers + [l.box for l in out.labels] + (links if strict else [])):
                        placed = Label(i, text, sz, b, not strict)
                        break
                if placed:
                    break
            if placed or n.step == 2:
                break
        if placed:
            out.labels.append(placed)
        else:
            out.dropped.append(f"{plan.tree.nodes[i].name} (step {n.step})")


def sector_of(dx: float, dy: float) -> int:
    return round(math.atan2(dy, dx) / (math.pi / 4)) % 8


def layout_badges(plan: Plan, avoid: list[Box]) -> list[Badge]:
    """Hidden linked nodes per 8 screen directions from the centre node, as "→ N개 더" at the window edge: the up ones in the
    title strip right of the title, the down ones just above the legend, the side ones on the side edges; each slides along
    its edge to the free place nearest its direction (off `avoid`, the marks)."""
    counts, lead = [0] * 8, [False] * 8
    path_next = next((i for i in plan.path if i not in plan.nodes), -1)
    for i, (rx, ry) in plan.hidden.items():
        s = sector_of(rx, ry)
        counts[s] += 1
        lead[s] |= i == path_next
    cb = content_box()
    title_end = PAD + te.width(r.S["tree_title"], Style(r.TITLE_SIZE)) + 10
    out: list[Badge] = []
    for s in (6, 5, 7, 2, 3, 1, 0, 4):
        n = counts[s]
        if not n:
            continue
        text = f"{ARROWS[s]} {n}개 더"
        w, h = text_box_size(text, BADGE_SIZE)
        side = s in (0, 4)
        if s in (5, 6, 7):
            y0 = (TITLE_H - h) / 2 + 1
            lo, hi = {5: (title_end, 140.0), 6: (title_end, WIN_W - 4 - w), 7: (150.0, WIN_W - 4 - w)}[s]
            pref = {5: lo, 6: (WIN_W - w) / 2, 7: hi}[s]
            spots = [(x, y0) for x in np.arange(lo, max(lo, hi) + 0.1, 2.0)]
        elif s in (1, 2, 3):
            y0 = cb.b + PAD / 2 - h
            lo, hi = {3: (3.0, 100.0), 2: (60.0, WIN_W - 63 - w), 1: (160.0 - w, WIN_W - 3 - w)}[s]
            pref = {3: lo, 2: (WIN_W - w) / 2, 1: hi}[s]
            spots = [(x, y) for x in np.arange(lo, max(lo, hi) + 0.1, 2.0) for y in (y0, y0 - 8.0)]
        else:
            x0 = WIN_W - 3 - w if s == 0 else 3.0
            pref = (cb.t + cb.b - h) / 2
            spots = [(x0, y) for y in np.arange(cb.t - 4, cb.b + PAD / 2 - h + 0.1, 2.0)]
        y_first = spots[0][1]
        spots.sort(key=lambda xy: abs((xy[1] if side else xy[0]) - pref) + (0.0 if side or xy[1] == y_first else 30.0))
        chosen = None
        for x, y in spots:
            b = Box(x, y, x + w, y + h)
            if not any(b.hits(o) for o in avoid + [g.box for g in out]):
                chosen = b
                break
        if chosen is None:
            x, y = spots[0]
            chosen = Box(x, y, x + w, y + h)
        out.append(Badge(text, chosen, lead[s]))
    return out


def tooltip_lines(plan: Plan, n: N) -> list[tuple[str, float, int]]:
    node = plan.tree.nodes[n.index]
    state = STATE_TEXT.get(n.state, "?") + (f" · {n.pct:.0f}%" if n.state == State.TARGET else "")
    colour = r.GOLD if n.state == State.MASTERED else r.WHITE if n.state == State.AVAILABLE else 0x9A9A9A
    return [(node.name, 11.0, r.WHITE),
            (f"{r.SCHOOL_TEXT.get(node.school, node.school)} · {r.LEVEL_TEXT.get(node.skill_level, node.skill_level)}", 9.5, r.TITLE_COLOR),
            (state, 9.5, colour)]


def place_tooltip(plan: Plan, n: N, lay: Layout) -> tuple[Box, int]:
    """The hover box near the node where it hides least: the centre, the target and step-1 marks and labels weigh most."""
    lines = tooltip_lines(plan, n)
    w = max(te.width(t, Style(s)) for t, s, _ in lines) + 12
    h = sum(s * te.LINE_PITCH for _, s, _ in lines) + 8
    window = Box(3.0, TITLE_H, WIN_W - 3.0, WIN_H - LEGEND_H - 1)
    ring = NODE_R + RING_GAP + 3.0
    weights = []
    for i in plan.order:
        m = plan.nodes[i]
        if i != n.index:
            weights.append((mark_box(m), 20 if m.step == 0 or m.state == State.TARGET else 8 if m.step == 1 else 2))
    for l in lay.labels:
        m = plan.nodes[l.index]
        weights.append((l.box, 12 if m.step == 0 or m.state == State.TARGET else 5 if m.step == 1 else 1))
    weights += [(b.box, 4) for b in lay.badges]
    best = None
    for bx in np.arange(window.l, window.r - w + 0.1, 2.0):
        for by in np.arange(window.t, window.b - h + 0.1, 2.0):
            b = Box(bx, by, bx + w, by + h)
            if b.hits(Box(n.x - ring, n.y - ring, n.x + ring, n.y + ring)):
                continue
            gap = max(b.l - n.x, n.x - b.r, 0) + max(b.t - n.y, n.y - b.b, 0)
            if gap > 45:
                continue
            key = (sum(wt for o, wt in weights if b.hits(o)), gap)
            if best is None or key < best[0]:
                best = (key, b)
    if best is None:
        return Box(window.l, window.t, window.l + w, window.t + h), -1
    return best[1], best[0][0]


def win_canvas(bg: Image.Image) -> r.Canvas:
    c = r.Canvas(bg, SS)
    c.oy = r.STAGE_Y0          # Canvas.p adds -STAGE_Y0; this makes window-local stage units map to the image
    return c


def mark(c: r.Canvas, x: float, y: float, state: State, pct: float, fade: float) -> None:
    gold, white, blue, grey = (mix(v, fade) for v in (r.GOLD, r.WHITE, r.BLUE, r.LOCKED_EDGE))
    d = r.diamond_pts(x, y, NODE_R)
    if state == State.MASTERED:
        c.polygon(d, gold)
        c.polyline(d, MARK_LINE, gold)
    elif state in (State.TARGET, State.LEARNING, State.READY):
        c.polyline(d, MARK_LINE, white)
        c.polyline(r.arc_pts(x, y, NODE_R + RING_GAP, 1.0 if state == State.READY else pct / 100.0), ARC_W, blue)
    elif state == State.AVAILABLE:
        c.polyline(d, MARK_LINE, white)
    else:  # locked: build 9's faint L4 outline (kLockedEdge), not the padlock
        c.polyline(d, MARK_LINE, grey)


def dashed(c: r.Canvas, a: tuple, b: tuple, w: float, rgb: int) -> None:
    L = math.dist(a, b)
    if L < 1e-6:
        return
    ux, uy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
    s = 0.0
    while s < L:
        e = min(s + r.DASH * 1.3, L)
        c.polyline([(a[0] + ux * s, a[1] + uy * s), (a[0] + ux * e, a[1] + uy * e)], w, rgb)
        s += (r.DASH + r.DASH_GAP) * 1.3


def draw_text(c: r.Canvas, box: Box, text: str, size: float, rgb: int, align: str = "left") -> None:
    st = Style(size)
    w = te.width(text, st)
    x = box.l + te.GUTTER if align == "left" else box.r - te.GUTTER - w if align == "right" else (box.l + box.r - w) / 2
    c.text_line(x, box.t + te.GUTTER / 2 + te.BASELINE * size, text, st, rgb)


def legend(c: r.Canvas) -> None:
    items = [(State.MASTERED, "숙달"), (State.TARGET, "학습 중"), (State.AVAILABLE, "배울 수 있음"), (State.LOCKED, "잠김")]
    st = Style(LEGEND_SIZE)
    gap, icon = 9.0, 2 * (LEGEND_ICON_R + 2.0)
    total = sum(icon + 2 + te.width(t, st) for _, t in items) + gap * (len(items) - 1)
    x = (WIN_W - total) / 2
    y = WIN_H - LEGEND_H / 2 - 2
    for state, text in items:
        cx = x + icon / 2
        d = r.diamond_pts(cx, y, LEGEND_ICON_R)
        if state == State.MASTERED:
            c.polygon(d, r.GOLD)
        elif state == State.TARGET:
            c.polyline(d, 1.0, r.WHITE)
            c.polyline(r.arc_pts(cx, y, LEGEND_ICON_R + 1.8, 0.7), 1.4, r.BLUE)
        else:
            c.polyline(d, 1.0, r.WHITE if state == State.AVAILABLE else r.LOCKED_EDGE)
        x += icon + 2
        c.text_line(x, y + LEGEND_SIZE * 0.35, text, st, r.DIM_TEXT)
        x += te.width(text, st) + gap


def clean_background(crop: Image.Image) -> Image.Image:
    """The real crop with its marks, text and legend filled from around them (the panel over the blurred frame is
    low-frequency): pixels brighter than their neighbourhood, grown by 6 px, are masked and filled by a normalized blur."""
    a = np.asarray(crop.convert("RGB").resize((260, 230), Image.LANCZOS)).astype(np.float32)
    lum = a.mean(axis=2)
    local = np.asarray(Image.fromarray(lum.astype(np.uint8)).filter(ImageFilter.MedianFilter(21))).astype(np.float32)
    mask = Image.fromarray(((lum > local + 5) * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(13))
    filled = estimate_background(a, np.asarray(mask) > 0, 10)
    small = Image.fromarray(np.clip(filled, 0, 255).astype(np.uint8))
    return small.resize((int(WIN_W * SS), int(WIN_H * SS)), Image.BICUBIC).filter(ImageFilter.GaussianBlur(4 * SS))


def layout(plan: Plan, hover: int) -> tuple[Layout, list[Box]]:
    """Badges, then the centre / step-1 labels: off every mark if they can be, else off the step-0/1 marks with the step-2
    marks under them left out (into a badge); then the step-2 labels where there is room."""
    hb = plan.nodes[hover]
    ring = NODE_R + RING_GAP + 3.0
    fixed = [Box(hb.x - ring - 1, hb.y - ring - 1, hb.x + ring + 1, hb.y + ring + 1), Box(0, WIN_H - LEGEND_H - 1, WIN_W, WIN_H)]
    centre = plan.nodes[plan.centre]

    def by_dist(ids):
        return sorted(ids, key=lambda i: math.dist((plan.nodes[i].x, plan.nodes[i].y), (centre.x, centre.y)))
    for _ in range(8):
        marks = [mark_box(plan.nodes[i]) for i in plan.order]
        marks1 = [mark_box(plan.nodes[i]) for i in plan.order if plan.nodes[i].step <= 1]
        lay = Layout(badges=layout_badges(plan, marks))
        victims = [i for i in plan.order if plan.nodes[i].step == 2 and any(mark_box(plan.nodes[i]).hits(b.box) for b in lay.badges)]
        first = [plan.centre] + by_dist(i for i in plan.order if plan.nodes[i].step == 1 and i != hover)
        links = link_boxes(plan)
        for i in first:
            tried = Layout(labels=list(lay.labels))
            place_labels(plan, [i], marks + fixed + [b.box for b in lay.badges], links, tried)
            if len(tried.labels) == len(lay.labels):
                place_labels(plan, [i], marks1 + fixed + [b.box for b in lay.badges], links, tried)
            if len(tried.labels) == len(lay.labels):
                lay.dropped.append(f"{plan.tree.nodes[i].name} (step {plan.nodes[i].step})")
                continue
            new = tried.labels[-1]
            lay.labels.append(new)
            victims += [j for j in plan.order if plan.nodes[j].step == 2 and mark_box(plan.nodes[j]).hits(new.box)]
        if not victims:
            break
        for j in dict.fromkeys(victims):
            drop(plan, j, "under a step-1 label or a badge")
        refresh_links(plan)
    marks = [mark_box(plan.nodes[i]) for i in plan.order]
    place_labels(plan, by_dist(i for i in plan.order if plan.nodes[i].step == 2), marks + fixed + [b.box for b in lay.badges],
                 link_boxes(plan), lay)
    return lay, marks


def render_new(row: Row, tree: mm.Tree, world: r.World, crop: Image.Image, out: list[str]) -> Image.Image:
    centre = r.node_index(tree, row.centre, row.school)
    target = r.node_index(tree, row.target, row.school)
    snap = mm.build_snapshot(tree, mm.PlayerState(world.known_ids(), {row.school: (tree.nodes[target].form_id, row.pct)},
                                                  world.required_xp), [row.school])
    plan = plan_tree(tree, snap, centre, row.school, target)
    hover = r.node_index(tree, row.hover, row.school)
    if hover not in plan.nodes or plan.nodes[hover].step != 1:
        raise ValueError(f"hover node {row.hover} is not a drawn step-1 node of {row.centre}")
    lay, marks = layout(plan, hover)
    tip, tip_cover = place_tooltip(plan, plan.nodes[hover], lay)

    out.append(f"== {row.centre}: scale {plan.scale:.3f} (build 9: 0.35), shown {len(plan.order)} "
               f"(step 1: {sum(n.step == 1 for n in plan.nodes.values())}, step 2: {sum(n.step == 2 for n in plan.nodes.values())}), "
               f"hidden linked {len(plan.hidden)}, path {' > '.join(tree.nodes[i].name for i in plan.path)}")
    out += ["  " + s for s in plan.notes]
    out += ["  left out: " + s for s in plan.left_out]
    for i in plan.order[1:]:
        n = plan.nodes[i]
        da = turn_deg(plan, n)
        out.append(f"  {tree.nodes[i].name}: step {n.step} {n.state.value}, turned {da:+.1f} deg around the centre{'  <-- >10' if abs(da) > 10 else ''}")
    out += [f"  badge {b.text}{' (학습 목표 쪽)' if b.lead else ''}" for b in lay.badges]
    out.append(f"  labels {len(lay.labels)} of {len(plan.order) - 1} (+centre), dropped: {', '.join(lay.dropped) or 'none'}")
    out.append(f"  tooltip at ({tip.l:.0f},{tip.t:.0f}) weighted cover {tip_cover} (0 = hides nothing)")

    img = clean_background(crop)
    c = win_canvas(img)
    path_edges = {frozenset(p) for p in zip(plan.path, plan.path[1:])}

    def edge_key(e):
        return 2 if frozenset(e[:2]) in path_edges else 0 if max(plan.nodes[e[0]].step, plan.nodes[e[1]].step) == 2 else 1
    for a, b, lock in sorted(plan.edges, key=edge_key):
        p, q = plan.nodes[a], plan.nodes[b]
        pa, pb = trimmed(p, q)
        on_path = frozenset((a, b)) in path_edges
        rgb = PATH_COLOR if on_path else (0x5C5C5C if State.LOCKED in (p.state, q.state) else r.EDGE_COLOR)
        if not on_path and max(p.step, q.step) == 2:
            rgb = mix(rgb, STEP2_FADE * 0.6)
        if lock and not on_path:
            dashed(c, pa, pb, EDGE_W, rgb)
        else:
            c.polyline([pa, pb], PATH_W if on_path else EDGE_W, rgb)
    for i in reversed(plan.order):
        n = plan.nodes[i]
        mark(c, n.x, n.y, n.state, n.pct, STEP2_FADE if n.step == 2 else 0.0)
    for b in lay.badges:
        c.rect_fill(b.box.l, b.box.t, b.box.r - b.box.l, b.box.b - b.box.t, 0x000000, 55)
        draw_text(c, b.box, b.text, BADGE_SIZE, PATH_COLOR if b.lead else 0xA8A8A8, "center")
    for l in lay.labels:
        n = plan.nodes[l.index]
        rgb = r.WHITE if n.step == 0 else PATH_COLOR if n.state == State.TARGET else 0x8A8A8A if n.state == State.LOCKED else r.LABEL_COLOR
        if n.step == 2 and n.state != State.TARGET:
            rgb = mix(rgb, 0.3)
        align = "right" if l.box.r <= n.x else "center" if l.box.l < n.x < l.box.r else "left"
        if l.over_link:
            c.rect_fill(l.box.l, l.box.t + 1, l.box.r - l.box.l, l.box.b - l.box.t - 1, 0x000000, 70)
        draw_text(c, l.box, l.text, l.size, rgb, align)
    hn = plan.nodes[hover]
    ring = NODE_R + RING_GAP + 3.0
    x0, y0 = c.p(hn.x - ring, hn.y - ring)
    x1, y1 = c.p(hn.x + ring, hn.y + ring)
    c.draw.ellipse((x0, y0, x1, y1), outline=r.rgb_t(HOVER_COLOR), width=round(1.6 * SS))
    c.rect_fill(tip.l, tip.t, tip.r - tip.l, tip.b - tip.t, 0x000000, 92)
    c.polyline([(tip.l, tip.t), (tip.r, tip.t), (tip.r, tip.b), (tip.l, tip.b), (tip.l, tip.t)], 1.0, r.PANEL_EDGE)
    y = tip.t + 4
    for text, s, rgb in tooltip_lines(plan, hn):
        c.text_line(tip.l + 6, y + te.BASELINE * s, text, Style(s), rgb)
        y += s * te.LINE_PITCH
    legend(c)
    r.draw_field(c, PAD, PAD / 2, WIN_W - 2 * PAD, TITLE_H, r.S["tree_title"], r.TITLE_COLOR, r.TITLE_SIZE)
    out.append("  overlap check: " + overlap_check(plan, lay))
    return img.resize((int(WIN_W * 2), int(WIN_H * 2)), Image.LANCZOS)


def overlap_check(plan: Plan, lay: Layout) -> str:
    bad = []
    for i, a in enumerate(lay.labels):
        for b in lay.labels[i + 1:]:
            if a.box.hits(b.box):
                bad.append(f"label {a.text} x label {b.text}")
        for j in plan.order:
            if a.box.hits(mark_box(plan.nodes[j])):
                bad.append(f"label {a.text} x mark {plan.tree.nodes[j].name}")
        for g in lay.badges:
            if a.box.hits(g.box):
                bad.append(f"label {a.text} x badge {g.text}")
    for i, a in enumerate(plan.order):
        for b in plan.order[i + 1:]:
            na, nb = plan.nodes[a], plan.nodes[b]
            d = math.dist((na.x, na.y), (nb.x, nb.y))
            if d < reach(na) + reach(nb):
                bad.append(f"mark {plan.tree.nodes[a].name} x mark {plan.tree.nodes[b].name} ({d:.1f})")
    for j in plan.order:
        for g in lay.badges:
            if mark_box(plan.nodes[j]).hits(g.box):
                bad.append(f"mark {plan.tree.nodes[j].name} x badge {g.text}")
    gaps = [math.dist((plan.nodes[a].x, plan.nodes[a].y), (plan.nodes[b].x, plan.nodes[b].y)) for i, a in enumerate(plan.order) for b in plan.order[i + 1:]]
    return ("OK" if not bad else "; ".join(bad)) + f" | smallest centre gap {min(gaps):.1f} units"


# ------------------------------------------------------------------------------------------------------------
# Sheets
# ------------------------------------------------------------------------------------------------------------

def font(px: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(te.FONT_PATH), px)


def tree_sheet(rows: list[tuple[Row, Image.Image, Image.Image]], path: Path) -> None:
    pw, ph = int(WIN_W * 2), int(WIN_H * 2)
    pad, head, cap = 24, 64, 56
    W = 2 * pw + 3 * pad
    H = head + len(rows) * (cap + ph + pad) + pad
    sheet = Image.new("RGB", (W, H), (18, 18, 18))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 14), "지금 (9판 실제 화면)", font=font(32), fill=(240, 216, 144))
    d.text((2 * pad + pw, 14), "개선안 (목업)", font=font(32), fill=(140, 200, 255))
    y = head
    for row, old, new in rows:
        d.text((pad, y + 10), row.caption, font=font(30), fill=(220, 220, 220))
        y += cap
        sheet.paste(old, (pad, y))
        sheet.paste(new, (2 * pad + pw, y))
        y += ph + pad
    sheet.save(path)


def estimate_background(a: np.ndarray, mask: np.ndarray, blur: float) -> np.ndarray:
    """The panel-over-frame colour under the masked (effect) pixels, by normalized blur of the unmasked ones."""
    keep = (~mask).astype(np.float32)
    out = np.zeros_like(a)
    wimg = Image.fromarray((keep * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(blur))
    wv = np.asarray(wimg).astype(np.float32) / 255.0
    for k in range(3):
        ch = Image.fromarray(np.clip(a[..., k] * keep, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(blur))
        out[..., k] = np.asarray(ch).astype(np.float32) / np.maximum(wv, 1e-3)
    return out


def halo_options(video9: Path, out_dir: Path, out: list[str]) -> None:
    """Three preview boxes from the real build-9 crop (halo_b9_heal.png = right half of halo_compare.png)."""
    src = Image.open(video9 / "halo_b9_heal.png").convert("RGB")
    a = np.asarray(src).astype(np.float32)
    h, w = a.shape[:2]
    lum = a.mean(axis=2)
    # the black blob: the largest connected region darker than 6 (its top is under the smoke, so its centre comes from
    # its width and its bottom); the backing is centred on it
    dark = lum < 6
    lab, count = nd.label(dark)
    if count == 0:
        raise ValueError("halo: no dark blob found in halo_b9_heal.png")
    blob = lab == (int(np.argmax(nd.sum(dark, lab, range(1, count + 1)))) + 1)
    ys, xs = np.nonzero(blob)
    blob_r = (xs.max() - xs.min()) / 2
    bx, by = (xs.max() + xs.min()) / 2, ys.max() - blob_r
    yy, xx = np.mgrid[0:h, 0:w]
    rr = np.hypot(yy - by, xx - bx)
    panel_lvl = float(np.median(lum[rr > blob_r + 60]))
    effect = (rr < blob_r + 12) | (lum > panel_lvl + 25)
    effect[: int(TITLE_H * 2)] = False      # the title strip is panel, not effect
    bg = estimate_background(a, effect, 40)
    box_h = 230.0 * 2          # preview box 67..297 at 2x (the crop shows its top 190 units)
    R = 0.48 * box_h
    plateau = blob_r + 8
    t = np.clip((rr - plateau) / max(R - plateau, 1.0), 0, 1)
    alpha = 1 - (t * t * (3 - 2 * t))      # 1 over the blob, smooth to 0 at R
    out.append(f"halo: blob centre ({bx / 2:.1f},{by / 2:.1f}) units in the crop, radius {blob_r / 2:.1f} units, panel level {panel_lvl:.1f}; "
               f"backing radius {R / 2:.1f} units (0.48 x 230), full black to {plateau / 2:.1f} units then smoothstep")
    # additive effect over a panel: pixel = background + effect, so a backing of alpha a behind it gives pixel - a * background.
    # Inside the blob the background is already hidden (pixel = effect over black): nothing to take away there. The blob's
    # top half is under the smoke and not dark, so there it is taken as the disc of its radius (estimate).
    filled = nd.binary_fill_holes(blob)
    occluded = filled | ((rr < blob_r - 3) & (yy < by))
    open_ = 1.0 - np.asarray(Image.fromarray((occluded * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2))).astype(np.float32) / 255
    opt1 = np.clip(a - (alpha * open_)[..., None] * bg, 0, 255)
    opt2 = np.clip(a - open_[..., None] * bg, 0, 255)
    # the video's colour fringe along the blob's hard edge shows once the panel colour is gone: grey it (dark pixels only)
    band = nd.binary_dilation(filled, iterations=5) & ~nd.binary_erosion(filled, iterations=3) & (lum < 40)
    for img in (opt1, opt2):
        g = img[band].mean(axis=1, keepdims=True)
        img[band] = np.repeat(g, 3, axis=1)
    ring_in = nd.binary_erosion(filled, iterations=4) & ~nd.binary_erosion(filled, iterations=10) & (yy > by)
    ring_out = nd.binary_dilation(filled, iterations=10) & ~nd.binary_dilation(filled, iterations=4) & (yy > by)
    for name, img in (("now", a), ("option 1", opt1), ("option 2", opt2)):
        L = img.mean(axis=2)
        out.append(f"blob edge contrast {name}: lower half just outside {L[ring_out].mean():.1f} vs just inside {L[ring_in].mean():.1f}")
    # keep the title text as drawn (it is over the panel, not added to it): restore the title strip pixels brighter than the panel
    title = np.zeros((h, w), bool)
    title[: int(TITLE_H * 2)] = True
    title &= lum > panel_lvl + 30
    opt2[title] = a[title]
    panels = [("지금 (9판)", src), ("안 1: 이펙트 뒤 부드러운 검은 원", Image.fromarray(opt1.astype(np.uint8))),
              ("안 2: 프리뷰 창만 불투명", Image.fromarray(opt2.astype(np.uint8)))]
    for i, (_, img) in enumerate(panels):
        img.save(out_dir / f"halo_panel{i}@2x.png")
    pad, cap = 20, 56
    sheet = Image.new("RGB", (3 * w + 4 * pad, h + cap + 2 * pad), (18, 18, 18))
    d = ImageDraw.Draw(sheet)
    for i, (label, img) in enumerate(panels):
        x = pad + i * (w + pad)
        d.text((x + 4, pad), label, font=font(26), fill=(240, 216, 144))
        sheet.paste(img, (x, pad + cap))
    sheet.save(out_dir / "halo_options.png")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tree", type=Path, default=Path("D:/TAKEALOOK/overwrite/SKSE/Plugins/SpellLearning/spell_tree.json"))
    ap.add_argument("--video9", type=Path, default=Path("C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video9"))
    ap.add_argument("--out", type=Path, default=Path("C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video9/mock"))
    args = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    for p in [args.tree, args.video9 / "halo_b9_heal.png"] + [args.video9 / row.crop for row in ROWS]:
        if not p.exists():
            log.error("input missing: %s", p)
            return 2
    args.out.mkdir(parents=True, exist_ok=True)
    tree = mm.load_tree(args.tree)
    world = r.World(tree, r.KNOWN_SPELLS, V.REQUIRED_XP)
    report: list[str] = []
    rows = []
    for row in ROWS:
        old = Image.open(args.video9 / row.crop).convert("RGB")
        new = render_new(row, tree, world, old, report)
        new.save(args.out / f"tree_new_{row.centre.replace(' ', '_')}@2x.png")
        old.save(args.out / f"tree_now_{row.centre.replace(' ', '_')}@2x.png")
        rows.append((row, old, new))
    tree_sheet(rows, args.out / "tree_compare.png")
    halo_options(args.video9, args.out, report)
    for line in report:
        log.info("%s", line)
    (args.out / "mock_log.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
