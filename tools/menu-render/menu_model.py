"""Offline port of SL_MagicMenu build 5's tree data and state rules (read-only model, no drawing).

Ported from plugins/SL_MagicMenu/src/TreeData.cpp (Parse/Build/NormalisePrereqs/Query/Find/SchoolRotationDeg)
and Spells.cpp (LookOf, PrereqOf, Learnable, IsDup, FindNode/PrimaryNode). The game state (which spells the
player knows, HoM progress) cannot be read offline; the caller passes it in as a PlayerState.
"""
from __future__ import annotations

import json
import logging
import math
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path

log = logging.getLogger(__name__)

DUP_MARKER = "_dup_"


class State(Enum):
    UNKNOWN = "unknown"
    LOCKED = "locked"
    AVAILABLE = "available"
    LEARNING = "learning"
    TARGET = "target"
    READY = "ready"
    MASTERED = "mastered"


SHOWN_STATES = {State.MASTERED, State.READY, State.TARGET, State.LEARNING, State.AVAILABLE}  # Widget.cpp Shown()


class Prereq(Enum):
    MASTERED = "mastered"
    NOT_MASTERED = "not_mastered"
    UNKNOWN = "unknown"
    NOT_IN_GAME = "not_in_game"


@dataclass
class Node:
    id: str
    form_id: int
    school: str
    name: str
    skill_level: str
    tier: int
    x: float
    y: float
    has_xy: bool
    is_root: bool
    hard: list[int] = field(default_factory=list)
    soft: list[int] = field(default_factory=list)
    soft_needed: int = 0


@dataclass
class Edge:
    a: int  # from (prerequisite / parent)
    b: int  # to
    lock: bool


@dataclass
class School:
    name: str
    spoke_deg: float | None
    root_index: int = -1
    nodes: list[int] = field(default_factory=list)


@dataclass
class Tree:
    nodes: list[Node]
    edges: list[Edge]
    schools: list[School]
    no_rotate: bool
    layout_mode: str
    by_form_id: dict[int, list[int]]
    by_id: dict[str, int]
    adjacent: list[list[int]]


def parse_form_id(text: str) -> int:
    """TreeData.cpp ParseFormId: "0x00012FCD[_dup_n]" -> int, 0 when not 1-8 hex digits."""
    cut = text.find(DUP_MARKER)
    if cut >= 0:
        text = text[:cut]
    if text[:2] in ("0x", "0X"):
        text = text[2:]
    if not text or len(text) > 8:
        return 0
    try:
        return int(text, 16)
    except ValueError:
        return 0


def is_dup(node: Node) -> bool:
    return DUP_MARKER in node.id


def _str_list(obj: dict, key: str) -> list[str]:
    value = obj.get(key)
    return [v for v in value if isinstance(v, str)] if isinstance(value, list) else []


def _num(obj: dict, key: str) -> float | None:
    value = obj.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return float(value) if math.isfinite(value) else None


def _other_ids(obj: dict, key: str, self_id: str) -> list[int]:
    out = []
    for text in _str_list(obj, key):
        if text == self_id:
            continue
        fid = parse_form_id(text)
        if fid:
            out.append(fid)
    return out


def _unique(ids: list[int]) -> list[int]:
    out: list[int] = []
    for i in ids:
        if i not in out:
            out.append(i)
    return out


def _normalise_prereqs(node: Node, obj: dict, school_root: str) -> None:
    """TreeData.cpp NormalisePrereqs."""
    prereqs = _other_ids(obj, "prerequisites", node.id)
    hard = _other_ids(obj, "hardPrereqs", node.id)
    soft = _other_ids(obj, "softPrereqs", node.id)
    sn = obj.get("softNeeded")
    soft_needed = min(int(sn) if isinstance(sn, (int, float)) and not isinstance(sn, bool) else 0, len(soft))
    if not hard and not soft and prereqs:
        hard = [prereqs[0]]
        if len(prereqs) > 1:
            soft = prereqs[1:]
            soft_needed = 1
    if node.is_root or (school_root and node.id == school_root):
        hard, soft, soft_needed = [], [], 0
    node.hard = _unique(hard)
    locks = [parse_form_id(l["nodeId"]) for l in (obj.get("locks") or []) if isinstance(l, dict) and isinstance(l.get("nodeId"), str)]
    node.soft = []
    for i in _unique(soft):
        if i not in node.hard:
            node.soft.append(i)
        elif i in locks:
            soft_needed -= 1
    node.soft_needed = max(0, min(soft_needed, len(node.soft)))


def find(tree: Tree, form_id: int, school: str = "") -> int:
    """TreeData.cpp Find: same school first, then non-dup."""
    best, best_score = -1, -1
    for index in tree.by_form_id.get(form_id, []):
        node = tree.nodes[index]
        score = (2 if school and node.school == school else 0) + (0 if is_dup(node) else 1)
        if score > best_score:
            best, best_score = index, score
    return best


def _resolve(tree: Tree, text: str, school: str) -> int:
    if text in tree.by_id:
        return tree.by_id[text]
    fid = parse_form_id(text)
    return find(tree, fid, school) if fid else -1


def load_tree(path: Path) -> Tree:
    """TreeData.cpp Load/Build. Schools in sorted key order like nlohmann::json."""
    with path.open(encoding="utf-8-sig") as handle:
        root = json.load(handle)
    schools_json = root.get("schools")
    if not isinstance(schools_json, dict):
        raise ValueError(f"{path}: no 'schools' object at the top level")
    tree = Tree([], [], [], bool(root.get("noRotate")), str(root.get("layoutMode") or ""), {}, {}, [])
    pending: list[tuple[int, dict]] = []
    root_texts: list[str] = []
    for school_name in sorted(schools_json):
        sj = schools_json[school_name]
        school = School(school_name, _num(sj, "spokeAngle"))
        root_texts.append(sj.get("root") if isinstance(sj.get("root"), str) else "")
        for nj in sj.get("nodes") or []:
            if not isinstance(nj, dict) or not isinstance(nj.get("formId"), str):
                continue
            index = len(tree.nodes)
            x, y = _num(nj, "x"), _num(nj, "y")
            node = Node(
                id=nj["formId"], form_id=parse_form_id(nj["formId"]), school=school_name,
                name=nj.get("name") if isinstance(nj.get("name"), str) else "",
                skill_level=nj.get("skillLevel") if isinstance(nj.get("skillLevel"), str) else "",
                tier=int(nj.get("tier") or 0), x=x or 0.0, y=y or 0.0, has_xy=x is not None and y is not None,
                is_root=bool(nj.get("isRoot")),
            )
            _normalise_prereqs(node, nj, root_texts[-1])
            tree.nodes.append(node)
            tree.by_id.setdefault(node.id, index)
            if node.form_id:
                tree.by_form_id.setdefault(node.form_id, []).append(index)
            school.nodes.append(index)
            pending.append((index, nj))
        tree.schools.append(school)

    seen: set[tuple[int, int, bool]] = set()

    def add(a: int, b: int, lock: bool) -> None:
        if a == b or (a, b, lock) in seen:
            return
        seen.add((a, b, lock))
        tree.edges.append(Edge(a, b, lock))

    for index, nj in pending:
        school = tree.nodes[index].school
        for text in _str_list(nj, "prerequisites"):
            other = _resolve(tree, text, school)
            if other >= 0:
                add(other, index, False)
        for text in _str_list(nj, "children"):
            other = _resolve(tree, text, school)
            if other >= 0:
                add(index, other, False)
        for lock in nj.get("locks") or []:
            if isinstance(lock, dict) and isinstance(lock.get("nodeId"), str):
                other = _resolve(tree, lock["nodeId"], school)
                if other >= 0:
                    add(other, index, True)
    tree.adjacent = [[] for _ in tree.nodes]
    for e, edge in enumerate(tree.edges):
        tree.adjacent[edge.a].append(e)
        tree.adjacent[edge.b].append(e)
    for s, school in enumerate(tree.schools):
        if root_texts[s]:
            index = _resolve(tree, root_texts[s], school.name)
            if index >= 0 and tree.nodes[index].school == school.name:
                school.root_index = index
                continue
        school.root_index = next((i for i in school.nodes if tree.nodes[i].is_root), -1)
    log.info("tree %s: %d nodes, %d edges", path, len(tree.nodes), len(tree.edges))
    return tree


def school_named(tree: Tree, name: str) -> School | None:
    return next((s for s in tree.schools if s.name == name), None)


def school_rotation_deg(tree: Tree, school: School) -> float:
    """TreeData.cpp SchoolRotationDeg: -90 - spokeAngle."""
    if tree.no_rotate or tree.layout_mode == "flat" or school.spoke_deg is None:
        return 0.0
    return -90.0 - school.spoke_deg


@dataclass
class Placed:
    index: int
    sx: float
    sy: float


@dataclass
class PlacedEdge:
    edge: int
    ax: float
    ay: float
    bx: float
    by: float
    a_shown: bool
    b_shown: bool


def query(tree: Tree, centre: int, rotation_deg: float, scale: float, half_w: float, half_h: float,
          max_steps: int, max_nodes: int) -> tuple[list[Placed], list[PlacedEdge]]:
    """TreeData.cpp Query."""
    count = len(tree.nodes)
    if centre < 0 or not tree.nodes[centre].has_xy:
        return [], []
    c = tree.nodes[centre]
    depth = [-1] * count
    depth[centre] = 0
    reached = [centre]
    head = 0
    while head < len(reached):
        cur = reached[head]
        head += 1
        if depth[cur] >= max_steps:
            continue
        for e in tree.adjacent[cur]:
            edge = tree.edges[e]
            other = edge.b if edge.a == cur else edge.a
            if depth[other] < 0:
                depth[other] = depth[cur] + 1
                reached.append(other)
    rad = math.radians(rotation_deg)
    cos_a, sin_a = math.cos(rad), math.sin(rad)
    offsets: dict[int, tuple[float, float]] = {}
    cands: list[Placed] = []
    for index in reached:
        node = tree.nodes[index]
        if not node.has_xy:
            continue
        dx, dy = node.x - c.x, node.y - c.y
        sx, sy = (dx * cos_a - dy * sin_a) * scale, (dx * sin_a + dy * cos_a) * scale
        offsets[index] = (sx, sy)
        if index == centre or (abs(sx) <= half_w and abs(sy) <= half_h):
            cands.append(Placed(index, sx, sy))
    rest = sorted(cands[1:], key=lambda p: (p.sx * p.sx + p.sy * p.sy, p.index))
    nodes = ([cands[0]] + rest)[: max(1, max_nodes)]
    shown = {p.index for p in nodes}
    taken: set[int] = set()
    edges: list[PlacedEdge] = []
    for p in nodes:
        for e in tree.adjacent[p.index]:
            edge = tree.edges[e]
            if e in taken or depth[edge.a] < 0 or depth[edge.b] < 0 or not tree.nodes[edge.a].has_xy or not tree.nodes[edge.b].has_xy:
                continue
            taken.add(e)
            a, b = offsets[edge.a], offsets[edge.b]
            edges.append(PlacedEdge(e, a[0], a[1], b[0], b[1], edge.a in shown, edge.b in shown))
    return nodes, edges


# ----------------------------------------------------------------------------------------------------------------
# Game state (assumed offline) and Spells.cpp's rules over it
# ----------------------------------------------------------------------------------------------------------------

@dataclass
class SpellState:
    mastered: bool
    percent: float
    required: float
    known: bool


@dataclass
class PlayerState:
    """What the game would give: the spells the player knows (by saved FormID) and HoM's learning targets."""
    known: set[int]
    targets: dict[str, tuple[int, float]]  # school -> (form id, percent)
    required_xp: dict[str, float]  # skill level word -> HoM RequiredXP (ASSUMED except Novice)


@dataclass
class Snapshot:
    spells: dict[int, SpellState]
    schools: dict[str, tuple[int, float, float]]  # school -> (target, percent, required)

    def target(self, school: str) -> int:
        return self.schools.get(school, (0, 0.0, 0.0))[0]


def build_snapshot(tree: Tree, player: PlayerState, schools: list[str]) -> Snapshot:
    """Spells.cpp AddSchool: every node of the school, its prerequisites, the target. The ids here are saved
    FormIDs (the game log 2026-10-25 resolved all 1410 nodes by persistent id, so one id per spell holds)."""
    by_id_level: dict[int, str] = {}
    for node in tree.nodes:
        by_id_level.setdefault(node.form_id, node.skill_level)
    snap = Snapshot({}, {})

    def spell(fid: int) -> SpellState:
        known = fid in player.known
        target_pct = next((pct for t, pct in player.targets.values() if t == fid), 0.0)
        required = player.required_xp.get(by_id_level.get(fid, ""), 0.0)
        # HoM IsSpellMastered is true for a known spell (outside early-learned tracking, assumed empty here)
        return SpellState(mastered=known, percent=100.0 if known else target_pct, required=required, known=known)

    for name in schools:
        school = school_named(tree, name)
        if not school:
            continue
        target, pct = player.targets.get(name, (0, 0.0))
        req = player.required_xp.get(by_id_level.get(target, ""), 0.0) if target else 0.0
        snap.schools[name] = (target, pct, req)
        if target:
            snap.spells.setdefault(target, spell(target))
        for index in school.nodes:
            node = tree.nodes[index]
            snap.spells.setdefault(node.form_id, spell(node.form_id))
            for saved in node.hard + node.soft:
                rid = find_saved(tree, saved, node.school)
                if rid:
                    snap.spells.setdefault(rid, spell(rid))
    return snap


def find_saved(tree: Tree, saved: int, school: str) -> int:
    index = find(tree, saved, school)
    return tree.nodes[index].form_id if index >= 0 else 0  # not in tree: treated as not in game (unparsed ids 0)


def prereq_of(tree: Tree, snap: Snapshot, saved: int, school: str) -> Prereq:
    fid = find_saved(tree, saved, school)
    if not fid:
        return Prereq.NOT_IN_GAME
    st = snap.spells.get(fid)
    if st is None:
        return Prereq.UNKNOWN
    return Prereq.MASTERED if st.mastered else Prereq.NOT_MASTERED


def prereqs_met(tree: Tree, snap: Snapshot, node: Node) -> bool:
    if not node.hard and not node.soft:
        return True
    if any(prereq_of(tree, snap, s, node.school) != Prereq.MASTERED for s in node.hard):
        return False
    if not node.soft:
        return True
    return sum(prereq_of(tree, snap, s, node.school) == Prereq.MASTERED for s in node.soft) >= node.soft_needed


def look_of(tree: Tree, snap: Snapshot, index: int) -> tuple[State, float]:
    """Spells.cpp LookOf."""
    node = tree.nodes[index]
    st = snap.spells.get(node.form_id)
    if st is None:
        return State.UNKNOWN, 0.0
    if st.mastered and (st.percent < 100.0 or st.known):
        return State.MASTERED, 100.0
    if st.percent >= 100.0 and not st.known:
        return State.READY, st.percent
    if node.form_id == snap.target(node.school):
        return State.TARGET, st.percent
    if st.known:
        return State.LEARNING, st.percent
    if prereqs_met(tree, snap, node):
        return State.AVAILABLE, st.percent
    return State.LOCKED, st.percent


def learnable(tree: Tree, snap: Snapshot, school_name: str) -> list[int]:
    """Spells.cpp Learnable: non-dup kAvailable nodes sorted by (tier, name, formId)."""
    school = school_named(tree, school_name)
    if not school:
        return []
    nodes = [i for i in school.nodes if not is_dup(tree.nodes[i]) and look_of(tree, snap, i)[0] == State.AVAILABLE]
    return sorted(nodes, key=lambda i: (tree.nodes[i].tier, tree.nodes[i].name.encode("utf-8"), tree.nodes[i].form_id))


def primary_node(tree: Tree, form_id: int) -> int:
    """Spells.cpp PrimaryNode: the first non-dup node of the id."""
    indexes = tree.by_form_id.get(form_id, [])
    return next((i for i in indexes if not is_dup(tree.nodes[i])), indexes[0] if indexes else -1)


def nodes_named(tree: Tree, name: str) -> list[int]:
    return [i for i, n in enumerate(tree.nodes) if n.name == name]
