"""score_proto.py - offline prototype of the spell gate scorer (ToDo.md M9, step 3).
Naive Bayes over record features and effect-description words, measured leave-one-plugin-out
against labels.json. Not shipped (lab/)."""
import json, collections, math, re, sys

ROUND = sys.argv[1] if len(sys.argv) > 1 else '2026-10-05'
DUMP = 'D:/TAKEALOOK/overwrite/SKSE/Plugins/SpellLearning/spell_scan_output.json'
LONG_SUMMON = 3600          # a summon over an hour is a companion or a mount, not a fight
labels = json.load(open(f'{ROUND}/labels.json', encoding='utf-8'))
spells = {s['persistentId']: s for s in json.load(open(DUMP, encoding='utf-8'))['spells']}

def words(text):
    return {w for w in re.findall(r'[가-힣A-Za-z]{2,}', text or '')}

def feats(s, text=True):
    f = set(); ef = s['effects']; arch = {e.get('archetype') for e in ef}
    cost = s.get('magickaCost') or 0
    if cost < 0.5: f.add('free')
    summons = [e for e in ef if e.get('archetype') in ('SummonCreature', 'Reanimate')]
    if summons and max(e.get('duration') or 0 for e in summons) > LONG_SUMMON: f.add('longSummon')
    for e in ef:
        for k in e.get('summonedKeywords') or []:
            if 'Horse' in k: f.add('summonsHorse')
    if arch <= {'Script'}: f.add('scriptOnly')
    if arch <= {'DetectLife'}: f.add('detectOnly')
    if 'SpawnScriptedRef' in arch: f.add('spawnsRef')
    if 'Banish' in arch and len(arch) == 1: f.add('banishOnly')
    if text:
        for e in ef:
            for w in words(e.get('description')): f.add('w:' + w)
        for w in words(s.get('name')): f.add('n:' + w)
    return f

def train(rows):
    cnt = {'in': collections.Counter(), 'out': collections.Counter()}; n = collections.Counter()
    for f, l, _ in rows:
        n[l] += 1; cnt[l].update(f)
    return cnt, n

def score(model, f):
    cnt, n = model; s = math.log((n['out'] + 1) / (n['in'] + 1))
    for x in f:
        if cnt['out'][x] + cnt['in'][x] < 2: continue      # a word seen once teaches nothing
        s += math.log((cnt['out'][x] + 0.5) / (n['out'] + 1)) - math.log((cnt['in'][x] + 0.5) / (n['in'] + 1))
    return s

def evaluate(text):
    data = [(feats(spells[i], text), v['label'], v['plugin']) for i, v in labels.items() if v['label'] in ('in', 'out')]
    res = []
    for p in sorted({r[2] for r in data}):
        m = train([r for r in data if r[2] != p])
        res += [(score(m, f), l) for f, l, q in data if q == p]
    for th in (2, 4, 6):
        tp = sum(1 for s, l in res if s > th and l == 'out'); fp = sum(1 for s, l in res if s > th and l == 'in')
        print(f'  text={text} th={th}: caught {tp}/89, false out {fp}')

base = [(feats(spells[i], False), v['label']) for i, v in labels.items() if v['label'] in ('in', 'out')]
print('free rule:', sum(1 for f, l in base if 'free' in f and l == 'out'), 'caught, false',
      sum(1 for f, l in base if 'free' in f and l == 'in'))
evaluate(False); evaluate(True)
