"""Which spell tomes can a player actually get? Read-only pass over the MO2 load order.

A tome counts as obtainable when anything refers to it: a placed reference (REFR),
a leveled list (LVLI), a container or NPC inventory (CNTO), a form list (FLST),
a crafting recipe (COBJ), a script property (any 4-byte form id inside a VMAD -
loose on purpose: a false hit only keeps a tome), or a SPID/KID ini line naming it.
Prints the tree spells (taughtByTome, not voiceSlot, in the scan dump) whose every
tome is unreferenced, grouped by plugin.
"""
import json, mmap, os, re, struct, sys, zlib
from collections import defaultdict

ROOT = r'D:\TAKEALOOK'
PROFILE = 'TKL - MUNG ADDON'
DUMP = ROOT + r'\overwrite\SKSE\Plugins\SpellLearning\spell_scan_output.json'
GAME_DATA = ROOT + r'\Stock Game\Data'

REC = struct.Struct('<4sIIIIHH')
SUB = struct.Struct('<4sH')

def read_lines(p):
    with open(p, encoding='utf-8', errors='replace') as f:
        return [l.rstrip('\r\n') for l in f]

# ---- files: highest priority mod wins (top of modlist.txt = highest)
prof = os.path.join(ROOT, 'profiles', PROFILE)
mods = [l[1:] for l in read_lines(os.path.join(prof, 'modlist.txt')) if l.startswith('+')]
where = {}
for name in os.listdir(GAME_DATA):
    where[name.lower()] = os.path.join(GAME_DATA, name)
dirs = [os.path.join(ROOT, 'mods', m) for m in reversed(mods)] + [os.path.join(ROOT, 'overwrite')]
ini_files = []
for d in dirs:
    if not os.path.isdir(d):
        continue
    for name in os.listdir(d):
        low = name.lower()
        p = os.path.join(d, name)
        if low.endswith(('.esp', '.esm', '.esl')):
            where[low] = p

    for sub, _dn, files in os.walk(d):
        if 'dynamicstringdistributor' in sub.lower() or 'fomod' in sub.lower():
            continue
        for fn in files:
            fl = fn.lower()
            sl = sub.lower()
            dist = fl.endswith(('_distr.ini', '_kid.ini', '_flm.ini', '_swap.ini', '_cid.ini', '_cdf.ini', '_lli.ini'))                 or 'skypatcher' in sl or 'containerdistributionframework' in sl or 'llos' in sl                 or 'leveledlist' in sl or 'containeritemdistributor' in sl
            if not dist or not fl.endswith(('.ini', '.json', '.toml', '.yaml', '.yml')):
                continue
            fp = os.path.join(sub, fn)
            try:
                if os.path.getsize(fp) <= 4 * 1024 * 1024:
                    ini_files.append(fp)
            except OSError:
                pass

active = set()
for l in read_lines(os.path.join(prof, 'plugins.txt')):
    if l.startswith('*'):
        active.add(l[1:].lower())
order = []
for l in read_lines(os.path.join(prof, 'loadorder.txt')):
    if not l or l.startswith('#'):
        continue
    low = l.lower()
    implicit = low in ('skyrim.esm', 'update.esm', 'dawnguard.esm', 'hearthfires.esm', 'dragonborn.esm') \
        or low.startswith('cc') or low == '_resourcepack.esl'
    if (low in active or implicit) and low in where:
        order.append(l)
print('plugins in load order:', len(order), file=sys.stderr)

# ---- parse
books = {}            # key -> (spellKey, edid, plugin that won)
refs = set()          # keys referred to
FID = struct.Struct('<I')

def resolve(fid, masters, self_name):
    idx = fid >> 24
    local = fid & 0xFFFFFF
    name = masters[idx] if idx < len(masters) else self_name
    if name.lower().endswith('.esl') or (idx >= len(masters) and self_is_light.get(self_name)):
        local &= 0xFFF
    return (name.lower(), local)

self_is_light = {}

def subrecords(data):
    i, n, big = 0, len(data), None
    while i + 6 <= n:
        t, size = SUB.unpack_from(data, i)
        i += 6
        if t == b'XXXX':
            big = FID.unpack_from(data, i)[0]; i += size; continue
        if big is not None:
            size, big = big, None
        yield t, data[i:i + size]
        i += size

def parse(path, name):
    with open(path, 'rb') as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    try:
        t, dsize, flags = struct.unpack_from('<4sII', mm, 0)
        head = mm[24:24 + dsize]
        masters = [bytes(v).rstrip(b'\0').decode('cp1252') for k, v in subrecords(head) if k == b'MAST']
        self_is_light[name] = bool(flags & 0x200) or name.lower().endswith('.esl')
        pos, end = 24 + dsize, len(mm)
        while pos + 24 <= end:
            typ, size, fl, fid, _a, _b, _c = REC.unpack_from(mm, pos)
            if typ == b'GRUP':
                pos += 24
                continue
            body = mm[pos + 24: pos + 24 + size]
            pos += 24 + size
            if typ not in WANT:
                continue
            if fl & 0x40000:
                try:
                    body = zlib.decompress(body[4:])
                except zlib.error:
                    continue
            key = resolve(fid, masters, name)
            handle(typ, key, body, masters, name)
    finally:
        mm.close()

WANT = {b'BOOK', b'REFR', b'ACHR', b'LVLI', b'CONT', b'NPC_', b'FLST', b'COBJ', b'QUST', b'INFO', b'ACTI',
        b'PERK', b'SCEN', b'PACK', b'MGEF', b'SPEL', b'MISC', b'ALCH', b'ARMO', b'WEAP', b'FURN', b'DOOR',
        b'FLOR', b'TACT', b'LIGH', b'ENCH', b'KEYM', b'AMMO', b'SCRL', b'INGR', b'SLGM'}

def handle(typ, key, body, masters, name):
    if typ == b'BOOK':
        edid, spell = '', None
        for t, d in subrecords(body):
            if t == b'EDID':
                edid = bytes(d).rstrip(b'\0').decode('cp1252', 'replace')
            elif t == b'DATA' and len(d) >= 8:
                if d[0] & 0x04:
                    spell = resolve(FID.unpack_from(d, 4)[0], masters, name)
        books[key] = (spell, edid)
    for t, d in subrecords(body):
        if typ == b'REFR' and t == b'NAME' and len(d) >= 4:
            refs.add(resolve(FID.unpack_from(d, 0)[0], masters, name))
        elif typ == b'LVLI' and t == b'LVLO' and len(d) >= 8:
            refs.add(resolve(FID.unpack_from(d, 4)[0], masters, name))
        elif t == b'CNTO' and len(d) >= 4:
            refs.add(resolve(FID.unpack_from(d, 0)[0], masters, name))
        elif typ == b'FLST' and t == b'LNAM' and len(d) >= 4:
            refs.add(resolve(FID.unpack_from(d, 0)[0], masters, name))
        elif typ == b'COBJ' and t == b'CNAM' and len(d) >= 4:
            refs.add(resolve(FID.unpack_from(d, 0)[0], masters, name))
        elif t == b'VMAD' or (typ == b'QUST' and t in (b'ALCO', b'ALFR', b'ALUA', b'CNTO')):
            b = bytes(d)
            for i in range(0, len(b) - 3):
                v = FID.unpack_from(b, i)[0]
                if v:
                    refs.add(resolve(v, masters, name))

for i, p in enumerate(order):
    try:
        parse(where[p.lower()], p)
    except Exception as e:
        print('skip', p, e, file=sys.stderr)
    if i % 500 == 0:
        print('..', i, file=sys.stderr)

# ---- SPID / KID ini lines
ini_text = []
for p in ini_files:
    try:
        ini_text.append(open(p, encoding='utf-8', errors='replace').read().lower())
    except OSError:
        pass
ini_all = '\n'.join(ini_text)

def in_ini(key, edid):
    if edid and re.search(r'(?<![\w])' + re.escape(edid.lower()) + r'(?![\w])', ini_all):
        return True
    plugin, local = key
    m = re.search(r'(?<![0-9a-fx])(0x)?0*%x\s*[~|:]\s*%s|%s\s*[~|:]\s*(0x)?0*%x(?![0-9a-f])' % (local, re.escape(plugin), re.escape(plugin), local), ini_all)
    return m is not None

# ---- tree spells and their tomes
spell_books = defaultdict(list)
for bkey, (skey, edid) in books.items():
    if skey:
        spell_books[skey].append((bkey, edid))

scan = json.load(open(DUMP, encoding='utf-8'))['spells']
lost = defaultdict(list)
evidence = defaultdict(lambda: defaultdict(int))
ini_only = []
checked = 0
for s in scan:
    if s.get('taughtByTome') is not True or s.get('voiceSlot'):
        continue
    plugin, local = s['persistentId'].split('|')
    skey = (plugin.lower(), int(local, 16))
    tomes = spell_books.get(skey, [])
    checked += 1
    if not tomes:
        lost['(no tome found by this pass)'].append(s['name'])
        continue
    why = [('record' if b in refs else 'ini' if in_ini(b, e) else '') for b, e in tomes]
    if any(why):
        evidence[s['plugin']][why[0] or why[-1] or 'x'] += 1
        if 'ini' in why and 'record' not in why:
            hit = ''
            for (b, e) in tomes:
                pl, lo = b
                pat = re.compile(r'(?<![0-9a-fx])(0x)?0*%x\s*[~|:]\s*%s|%s\s*[~|:]\s*(0x)?0*%x(?![0-9a-f])' % (lo, re.escape(pl), re.escape(pl), lo))
                for fp, txt in zip(ini_files, ini_text):
                    m = pat.search(txt)
                    if m:
                        a = max(0, m.start() - 60)
                        hit = fp.replace(ROOT, '') + ' :: ' + txt[a:m.end() + 20].replace(chr(10), ' ')
                        break
                if hit:
                    break
            ini_only.append(s['name'] + ' / ' + s['plugin'] + chr(10) + '        ' + hit)
        continue
    lost[s['plugin']].append(s['name'] + ' [' + ', '.join(e or '%s|%x' % b for b, e in tomes) + ']')

pex_plugins = set()
for d in dirs:
    sd = os.path.join(d, 'Scripts')
    if not os.path.isdir(sd):
        continue
    for fn in os.listdir(sd):
        if fn.lower().endswith('.pex'):
            try:
                b = open(os.path.join(sd, fn), 'rb').read().lower()
            except OSError:
                continue
            if b'getformfromfile' in b:
                for pl in set(k for k in lost if not k.startswith('(')):
                    if pl.lower().encode('cp1252', 'replace') in b:
                        pex_plugins.add(pl + ' (' + fn + ')')
print('plugins whose scripts call GetFormFromFile with their own name:', sorted(pex_plugins))
print('tree spells checked:', checked)
print('cleared only by a config file:', len(ini_only))
for n in ini_only: print('   ', n)
total = sum(len(v) for v in lost.values())
print('spells whose every tome nothing refers to:', total)
for plugin, names in sorted(lost.items(), key=lambda kv: -len(kv[1])):
    print('%4d  %s' % (len(names), plugin))
    for n in names:
        print('        ' + n)
