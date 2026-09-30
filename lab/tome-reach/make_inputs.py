"""Write tome-reach-test's inputs from an MO2 profile: the active plugins in load
order with the file MO2 would serve for each, and the data folders in priority
order (game Data, then mods low to high, then overwrite).

    python make_inputs.py <MO2 root> <profile> > inputs.json
"""
import json, os, sys

root, profile = sys.argv[1], sys.argv[2]
prof = os.path.join(root, 'profiles', profile)
game_data = os.path.join(root, 'Stock Game', 'Data')

def lines(name):
    with open(os.path.join(prof, name), encoding='utf-8', errors='replace') as f:
        return [l.rstrip('\r\n') for l in f]

mods = [l[1:] for l in lines('modlist.txt') if l.startswith('+')]
data_dirs = [game_data] + [os.path.join(root, 'mods', m) for m in reversed(mods)] + [os.path.join(root, 'overwrite')]
data_dirs = [d for d in data_dirs if os.path.isdir(d)]

where = {}
for d in data_dirs:
    for name in os.listdir(d):
        if name.lower().endswith(('.esp', '.esm', '.esl')):
            where[name.lower()] = (name, os.path.join(d, name))

active = {l[1:].lower() for l in lines('plugins.txt') if l.startswith('*')}
implicit = {'skyrim.esm', 'update.esm', 'dawnguard.esm', 'hearthfires.esm', 'dragonborn.esm', '_resourcepack.esl'}
plugins = []
for l in lines('loadorder.txt'):
    low = l.lower()
    if not l or l.startswith('#') or low not in where:
        continue
    if low in active or low in implicit or low.startswith('cc'):
        name, path = where[low]
        plugins.append({'name': name, 'path': path})

json.dump({'plugins': plugins, 'dataDirs': data_dirs}, sys.stdout, ensure_ascii=False)
