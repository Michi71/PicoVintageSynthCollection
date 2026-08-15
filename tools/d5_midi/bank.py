"""bank.py -- read the generated patch table back, for tools that need to know
what a patch is before playing it. Reads d5_patch_data.h, i.e. it needs a build
that had the bank dumps present."""
import os, re, glob

def _find():
    env = os.environ.get('D5_PATCH_DATA')
    if env:
        return env
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    hits = glob.glob(os.path.join(root, 'build*', '**', 'd5_patch_data.h'), recursive=True)
    if not hits:
        raise SystemExit('no d5_patch_data.h found -- build PicoFaceD5 first, '
                         'or point D5_PATCH_DATA at one')
    return max(hits, key=os.path.getmtime)

SRC = _find()
_t = open(SRC).read()
_n = re.search(r'kPatchNames\[384\]\[19\]\s*=\s*\{(.*?)\n\};', _t, re.S)
NAMES = re.findall(r'"((?:[^"\\]|\\.)*)"', _n.group(1))
_b = re.search(r'kPatchData\[[^\]]*\]\[[^\]]*\]\s*=\s*\{(.*)\n\};', _t, re.S)
_v = [int(x, 0) for x in re.findall(r'0x[0-9A-Fa-f]{2}|\b\d+\b', _b.group(1))]
DATA = [_v[i*448:(i+1)*448] for i in range(384)]

KM = {0:'WHOL',1:'DUAL',2:'SPLT',3:'SEP',4:'WHOL-S',5:'DUAL-S',6:'SPL-L',7:'SPL-U',8:'SEP-S'}
# structure: which partial is PCM (panel 1..7)
STRUCT_PCM = {1:(0,0), 2:(0,0), 3:(0,1), 4:(0,1), 5:(1,0), 6:(1,1), 7:(1,1)}
RING = {2,4,7}   # ring-modulating structures (panel numbering)

def blk(i, n): return DATA[i][n*64:(n+1)*64]

def info(i):
    pb = blk(i,6); uc = blk(i,2); lc = blk(i,5)
    d = dict(idx=i, bank=i//64+1, prog=i%64+1, name=NAMES[i])
    d['km'] = KM.get(pb[18], str(pb[18])); d['kmraw'] = pb[18]
    d['split'] = 36+pb[19]
    d['bend'] = pb[26]; d['atbend'] = pb[27]-12
    d['porta'] = (pb[41], pb[28])
    d['rev'] = pb[30]; d['revbal'] = pb[31]
    d['tonebal'] = pb[33]
    d['ushift'] = pb[22]-24; d['lshift'] = pb[23]-24
    for tag, c, p1, p2 in (('u', uc, blk(i,0), blk(i,1)), ('l', lc, blk(i,3), blk(i,4))):
        st = c[10]+1
        d[tag+'st'] = st
        d[tag+'mute'] = c[46] & 3
        d[tag+'chor'] = (c[42]+1, c[45])
        d[tag+'sync'] = (c[28], c[32], c[36])
        d[tag+'pmodat'] = c[24] if len(c) > 24 else 0
        pcm = STRUCT_PCM.get(st,(0,0))
        waves = []
        for k,(p,ispcm) in enumerate(((p1,pcm[0]),(p2,pcm[1]))):
            if ispcm: waves.append('P%d' % p[7])
            else: waves.append(('SQ' if p[6]==0 else 'SAW') + ('/pw%d'%p[8] if p[6]==0 else ''))
        d[tag+'w'] = waves
        d[tag+'coarse'] = (p1[0]-36, p2[0]-36)
        d[tag+'at'] = (p1[12]-7, p1[34]-7, p1[53]-7, p2[12]-7, p2[34]-7, p2[53]-7)
    d['ring'] = d['ust'] in RING or d['lst'] in RING
    return d

if __name__ == '__main__':
    import sys
    for i in range(384):
        d = info(i)
        if len(sys.argv)>1 and sys.argv[1] not in d['name'].lower() and sys.argv[1] != 'all': continue
        print("%3d %d-%02d %-19s %-6s st%d/%d %-10s %-10s rev%2d/%3d bnd%2d at%+3d sync%s" % (
            d['idx'], d['bank'], d['prog'], d['name'], d['km'], d['ust'], d['lst'],
            '+'.join(d['uw']), '+'.join(d['lw']), d['rev'], d['revbal'], d['bend'], d['atbend'], d['usync']))
