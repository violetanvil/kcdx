import zipfile, glob, os

GAME = r'E:/SteamLibrary/steamapps/common/KingdomComeDeliverance2'
data = os.path.join(GAME, 'Data')
eng  = os.path.join(GAME, 'Engine')

def bindroot_of(pakpath, root):
    # bind-root = pak's directory relative to the data/engine root, fwd-slash, lower
    rel = os.path.relpath(pakpath, root)
    d = os.path.dirname(rel)
    d = d.replace(os.sep, '/').replace('\\', '/')
    return d.lower()

bare = {}      # bare pe.name -> set of paks
bindroot = {}  # <bindroot>/<name> -> set of paks

for root, label in ((data, 'Data'), (eng, 'Engine')):
    if not os.path.isdir(root):
        print('MISSING', root)
        continue
    for pak in glob.glob(os.path.join(root, '**', '*.pak'), recursive=True):
        br = bindroot_of(pak, root)
        try:
            names = zipfile.ZipFile(pak).namelist()
        except Exception as e:
            print('ERR', pak, e)
            continue
        for n in names:
            n2 = n.replace('\\', '/').lower().rstrip('/')
            if not n2:
                continue
            bare.setdefault(n2, set()).add(os.path.basename(pak))
            key = (br + '/' + n2) if br else n2
            bindroot.setdefault(key, set()).add(os.path.basename(pak))

bare_coll = {k: v for k, v in bare.items() if len(v) > 1}
br_coll   = {k: v for k, v in bindroot.items() if len(v) > 1}

print('total bare keys     :', len(bare), ' bare cross-pak collisions    :', len(bare_coll))
print('total bindroot keys :', len(bindroot), ' bindroot cross-pak collisions:', len(br_coll))
print()
print('--- bare cross-pak collisions (what LAST-pak-wins silently masks TODAY) ---')
for k, v in list(bare_coll.items())[:20]:
    print('  ', k, '->', sorted(v))
print('  ...(%d total)' % len(bare_coll))
print()
print('--- bindroot cross-pak collisions (the FIX) ---')
for k, v in list(br_coll.items())[:20]:
    print('  ', k, '->', sorted(v))
print('  ...(%d total)' % len(br_coll))
print()
print('leveldata bindroot keys :', [k for k in bindroot if 'leveldata.xml' in k][:5])
print('engine config keys      :', [k for k in bindroot if 'engine_core' in k or k.startswith('config/')][:6])
print('shaders keys (sample)   :', [k for k in bindroot if k.startswith('shaders/')][:4])
