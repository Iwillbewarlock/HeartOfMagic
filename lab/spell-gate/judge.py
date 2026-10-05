"""
judge.py - the first labels for the spell gate (docs/librarian/ToDo.md, M9)

    python judge.py cards <spell_scan_output.json> <config.json> <out cards.json>
        The tree candidates (taught by a tome, not voice slot, not a copy - before the
        free-spell rule and the blacklist, so those count as known "out" labels) as
        compact cards.
    python judge.py run <cards.json> <out verdicts.json> [model] [batch]
        Asks the local Ollama model (default qwen3.5:9b) for each card: is it a spell a
        player learns from a tome and casts in play? Resumes: cards already in the
        verdicts file are skipped.

Plain Python, no packages. Not shipped (lab/).
"""
import json, sys, os, urllib.request

PROMPT = """You sort Skyrim spells. For each card answer: is this a spell a player learns from a spell tome and casts during play?

"in": a real castable spell - attacks, wards, heals, buffs, illusions, teleports the player uses, and summons of creatures,
daedra, undead, spirits or named boss spirits for a fight (about 60 seconds), even from mods.
"out": not a player spell - calling a follower, companion, pet or mount to you (or dismissing it), a mod's utility or
settings toggle (fix heels, open a menu, spawn food or a chest for convenience), a quest helper, a debug or test spell.
"unsure": the card does not say enough.

Answer only JSON: {"verdicts": [{"id": "<id>", "label": "in"|"out"|"unsure", "why": "<at most 12 words>"}, ...]} with one entry for EVERY card, same order.

Cards:
"""


def cards(scan_path, config_path, out_path):
    spells = json.load(open(scan_path, encoding='utf-8'))['spells']
    config = json.load(open(config_path, encoding='utf-8'))
    blacklist = {e.get('formId') for e in config.get('spellBlacklist', [])}
    out = []
    for s in spells:
        if s.get('taughtByTome') is False or s.get('voiceSlot') or s.get('duplicateOf'):
            continue
        effects = []
        for e in s.get('effects', []):
            if (e.get('flags') or {}).get('hideInUI'):
                continue
            effects.append({
                'archetype': e.get('archetype'), 'delivery': e.get('delivery'),
                'magnitude': e.get('magnitude'), 'duration': e.get('duration'),
                'description': (e.get('description') or '')[:200],
                'summons': [k for k in (e.get('summonedKeywords') or []) if k.startswith('ActorType')],
            })
        out.append({
            'id': s['persistentId'], 'name': s.get('name'), 'plugin': s.get('plugin'), 'school': s.get('school'),
            'level': s.get('skillLevel'), 'magicka': round(s.get('magickaCost') or 0), 'delivery': s.get('delivery'),
            'castingType': s.get('castingType'), 'halfCostPerk': bool(s.get('castingPerk')),
            'tomeUnreachable': bool(s.get('tomeUnreachable')), 'effects': effects,
            # known labels from the rules the player and the user already set
            'known': 'out' if (s['formId'] in blacklist or (s.get('magickaCost') or 0) < 0.5) else None,
        })
    json.dump(out, open(out_path, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print(len(out), 'cards,', sum(1 for c in out if c['known']), 'known out')


def ask(model, batch):
    view = [{k: v for k, v in c.items() if k not in ('known',)} for c in batch]
    body = json.dumps({
        'model': model, 'stream': False, 'think': False, 'format': 'json',
        'options': {'temperature': 0, 'num_ctx': 16384},
        'messages': [{'role': 'user', 'content': PROMPT + json.dumps(view, ensure_ascii=False)}],
    }).encode('utf-8')
    req = urllib.request.Request('http://127.0.0.1:11434/api/chat', data=body, headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=600) as r:
        text = json.load(r)['message']['content']
    data = json.loads(text)
    if isinstance(data, dict):  # format=json can wrap the list
        data = next((v for v in data.values() if isinstance(v, list)), [data])
    return data


def run(cards_path, out_path, model='qwen3.5:9b', batch=12):
    batch = int(batch)
    all_cards = json.load(open(cards_path, encoding='utf-8'))
    verdicts = json.load(open(out_path, encoding='utf-8')) if os.path.exists(out_path) else []
    done = {v['id'] for v in verdicts}
    todo = [c for c in all_cards if c['id'] not in done]
    for i in range(0, len(todo), batch):
        chunk = todo[i:i + batch]
        try:
            answers = ask(model, chunk)
        except Exception as e:  # one bad batch does not stop the round
            print('batch failed:', e, file=sys.stderr)
            continue
        by_id = {a.get('id'): a for a in answers if isinstance(a, dict)}
        for c in chunk:
            a = by_id.get(c['id'])
            if a and a.get('label') in ('in', 'out', 'unsure'):
                verdicts.append({'id': c['id'], 'name': c['name'], 'plugin': c['plugin'], 'label': a['label'],
                                 'why': a.get('why', ''), 'known': c['known'], 'model': model})
        json.dump(verdicts, open(out_path, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
        print(f'{len(verdicts)}/{len(all_cards)}', flush=True)


if __name__ == '__main__':
    if sys.argv[1] == 'cards':
        cards(*sys.argv[2:5])
    elif sys.argv[1] == 'run':
        run(*sys.argv[2:])
