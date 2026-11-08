from pathlib import Path
import json,statistics
cache={}; banks=[]
def info(p):
 p=p.resolve()
 if p in cache:return cache[p]
 with p.open('rb') as f:
  assert f.read(4)==b'fLaC'; h=f.read(4); d=f.read(int.from_bytes(h[1:],'big')); z=int.from_bytes(d[10:18],'big'); rate=z>>44; channels=((z>>41)&7)+1; samples=z&((1<<36)-1)
 cache[p]=(samples*channels*4,samples/rate)
 return cache[p]
for p in Path('assets/characters').rglob('session.nks.json'):
 paths=[]
 def walk(v):
  if isinstance(v,dict):
   for x in v.values():walk(x)
  elif isinstance(v,list):
   for x in v:walk(x)
  elif isinstance(v,str) and v.endswith('.flac'):paths.append(v)
 walk(json.loads(p.read_text()))
 if paths:
  clips=[info(p.parent/x) for x in dict.fromkeys(paths)]
  banks.append((sum(x[0] for x in clips),len(clips),str(p),sum(x[1] for x in clips)))
print('banks',len(banks),'unique clips',len(cache))
for x in sorted(banks,reverse=True)[:8]: print(x)
if banks:print('PCM MiB min/median/max', min(x[0] for x in banks)/2**20,statistics.median(x[0] for x in banks)/2**20,max(x[0] for x in banks)/2**20)
