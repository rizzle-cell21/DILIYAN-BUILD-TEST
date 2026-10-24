#!/usr/bin/env python3
"""Behavioral integration tests, independent FFmpeg/libsoxr checks. No dependencies beyond Python stdlib and FFmpeg on the build machine."""
from pathlib import Path
import argparse,array,json,math,struct,subprocess,sys,hashlib,wave,re,shutil,os
p=argparse.ArgumentParser();p.add_argument('engine',type=Path);p.add_argument('baseline',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
a.engine=a.engine.resolve();a.baseline=a.baseline.resolve();r=a.output;r.mkdir(parents=True,exist_ok=False)
assert shutil.which('ffmpeg'),'Independent FFmpeg meter is required on the build machine'
def command(exe,*args,ok=True):
 q=subprocess.run([str(exe)]+list(map(str,args)),capture_output=True,text=True,timeout=240)
 assert (q.returncode==0)==ok,(q.stdout,q.stderr)
 return q.stdout

def meter(path):return json.loads(command(a.engine,'--measure',path))
def write(path,fn,seconds=4,sr=48000,channels=2):
 values=array.array('h')
 for i in range(round(seconds*sr)):
  t=i/sr;x=fn(i,t);values.extend([max(-32768,min(32767,round(x*32768)))]*channels)
 if sys.byteorder!='little':values.byteswap()
 with wave.open(str(path),'wb') as w:w.setnchannels(channels);w.setsampwidth(2);w.setframerate(sr);w.writeframes(values.tobytes())

def read(path):
 b=path.read_bytes();offset=12;fmt=None
 while offset+8<=len(b):
  tag=b[offset:offset+4];n=struct.unpack_from('<I',b,offset+4)[0];payload=b[offset+8:offset+8+n]
  if tag==b'fmt ':fmt=struct.unpack_from('<HHIIHH',payload)
  if tag==b'data':
   assert fmt is not None
   encoding,ch,sr,_,_,bits=fmt
   if encoding==3:values=struct.unpack('<'+'f'*(len(payload)//4),payload)
   elif bits==24:values=[int.from_bytes(payload[i:i+3],'little',signed=True)/8388608 for i in range(0,len(payload),3)]
   else:raise AssertionError(fmt)
   return sr,ch,bits,values
  offset+=8+n+(n&1)
 raise AssertionError('Missing WAV data')

def independent(path):
 helper=os.environ.get('DILIYAN_SOXR_METER')
 if helper:
  peak=float(subprocess.check_output([helper,str(path)],text=True))
 else:
  q=subprocess.run(['ffmpeg','-hide_banner','-nostats','-i',str(path),'-af','aresample=768000:resampler=soxr:precision=33:cutoff=0.999,astats=metadata=0:reset=0','-f','null','-'],capture_output=True,text=True)
  assert q.returncode==0,q.stderr
  peaks=re.findall(r'Peak level dB:\s*(-?[\d.]+)',q.stderr);assert peaks,q.stderr
  peak=float(peaks[-1])
 q=subprocess.run(['ffmpeg','-hide_banner','-nostats','-i',str(path),'-af','ebur128=peak=true','-f','null','-'],capture_output=True,text=True,check=True)
 summary=q.stderr.rsplit('Summary:',1)[-1];lufs=float(re.search(r'I:\s+(-?[\d.]+) LUFS',summary).group(1))
 short_values=[float(x) for x in re.findall(r'S:\s+(-?[\d.]+)',q.stderr)]
 own=meter(path)
 if own['seconds']>=3:
  assert short_values and abs(max(short_values)-own['shortMax'])<.15,(max(short_values),own['shortMax'])
 return lufs,peak

v,b,ref=[r/name for name in ['Vocal.wav','Beat.wav','Reference.wav']]
def envelope(t):return [.35,.7,1,.5][min(3,int(t//6))]
def voice(i,t):return envelope(t)*(.07*math.sin(2*math.pi*230*t)+.025*math.sin(2*math.pi*2400*t))*(1 if t%3<2 else 0)
def beat(i,t):return envelope(t)*(.17*math.sin(2*math.pi*65*t)+.05*math.sin(2*math.pi*2600*t)+.03*math.sin(2*math.pi*7200*t)*math.exp(-30*(t%.25)))
write(v,voice,24);write(b,beat,24)
write(ref,lambda i,t:envelope(t)*(.18*math.sin(2*math.pi*1000*t)+.08*math.sin(2*math.pi*80*t)+.09*math.sin(2*math.pi*3500*t)),24)
base=meter(ref)['lufs'];factor=10**((-8.39-base)/20)
write(ref,lambda i,t:factor*envelope(t)*(.18*math.sin(2*math.pi*1000*t)+.08*math.sin(2*math.pi*80*t)+.09*math.sin(2*math.pi*3500*t)),24)
hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in [v,b,ref]}
project=r/'Reference Mix ü'
command(a.engine,'--mix',v,b,ref,project,.8,'vdbult')
mix=json.loads((project/'Mix.json').read_text());assert len(mix['files'])==4
assert mix['referenceMix']['pairedWindows']>=4 and mix['referenceMix']['confidence']>=.5
assert len(mix['referenceMix']['recommendations'])==6
assert len(mix['referenceMix']['ownSections'])==4
assert len(mix['referenceMix']['controlTrace'])==240
assert all(e['metrics']['frames']==1152000 for e in mix['files'])
assert (project/'Balance.wav').read_bytes()!=(project/'MixGlue.wav').read_bytes()
print('PASS reference is analysed before mixing; 4 measured energy windows, 6 decisions, real processed mix',flush=True)
command(a.engine,'--master',project,ref,'MixGlue',project/'Master')
master=json.loads((project/'Master/Master.json').read_text());assert len(master['files'])==7
for m in master['masters']:
 path=project/'Master'/m['file'];sr,ch,bits,samples=read(path)
 assert (sr,ch,bits)==(48000,2,24) and len(samples)==2304000
 assert max(map(abs,samples))<1 and all(math.isfinite(x) for x in samples)
 il,tp=independent(path)
 assert tp<=-1.0 and abs(il-m['metrics']['lufs'])<.15
 assert abs(tp-m['metrics']['truePeakEstimate'])<.12
 assert m['targetLUFS']> -10
 assert len(m['compressorTrace'])==240 and len(m['limiterTrace'])==240
 assert abs(max(p[1] for p in m['limiterTrace'])-m['limiterMaxDB'])<.0001
 print('PASS independent V3',m['file'],round(il,2),'LUFS,',round(tp,3),'dBTP, limiter max',round(m['limiterMaxDB'],3),'dB',flush=True)
# Same original sources/reference in an unmodified V2 binary, no inferred listening outcome.
command(a.baseline,'--mix',v,b,r/'V2',.8)
command(a.baseline,'--master',r/'V2',ref,'MixGlue',r/'V2/Master')
old=json.loads((r/'V2/Master/Master.json').read_text())
print('PASS same-fixture V2/V3 comparison:',json.dumps({'v2LUFS':[m['metrics']['lufs'] for m in old['masters']], 'v3LUFS':[m['metrics']['lufs'] for m in master['masters']], 'v2Mix':json.loads((r/'V2/Mix.json').read_text())['files'][2]['metrics'], 'v3Mix':mix['files'][2]['metrics']},sort_keys=True),flush=True)
command(a.engine,'--master',project,ref,'MixGlue',project/'Repeated')
for name in ['Master-A.wav','Master-B.wav','Master-C.wav']:assert (project/'Master'/name).read_bytes()==(project/'Repeated'/name).read_bytes()
command(a.engine,'--master',project,ref,'Balance',project/'Review')
assert json.loads((project/'Review/Master.json').read_text())['selectedMix']=='Balance'
command(a.engine,'--mix',v,b,ref,r/'Rejected',.8,'-')
assert (r/'Rejected/Balance.wav').read_bytes()==(r/'Rejected/MixGlue.wav').read_bytes()
assert not any(x['enabled'] for x in json.loads((r/'Rejected/Mix.json').read_text())['referenceMix']['recommendations'])
command(a.engine,'--mix',v,b,ref,r/'Bypass unmask',0,'u')
assert (r/'Bypass unmask/Balance.wav').read_bytes()==(r/'Bypass unmask/MixGlue.wav').read_bytes()
# Different reference has to change the MIX, before any mastering is run.
alt=r/'Dark reference.wav';write(alt,lambda i,t:envelope(t)*(.2*math.sin(2*math.pi*65*t)+.01*math.sin(2*math.pi*2800*t)),24)
command(a.engine,'--mix',v,b,alt,r/'Other reference',.8,'vdbult')
other=json.loads((r/'Other reference/Mix.json').read_text())
assert other['referenceMix']['vocalEQ']!=mix['referenceMix']['vocalEQ']
assert (r/'Other reference/MixGlue.wav').read_bytes()!=(project/'MixGlue.wav').read_bytes()
command(a.engine,'--master',project,alt,'MixGlue',r/'Wrong reference',ok=False)
assert not (r/'Wrong reference').exists()
command(a.engine,'--mix',v,b,ref,project,.8,'vdbult',ok=False)
command(a.engine,'--mix',v,v,ref,r/'Duplicate',.8,'vdbult',ok=False)
for path,digest in hashes.items():assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
print('PASS deterministic render, decision bypass, REVIEW selection, alternate-reference changes, immutable sources and stale-reference rejection',flush=True)
# Reuse the short high-frequency burst counterexample that exposed V1's old peak meter.
sv,sb,sr=[r/name for name in ['StressV.wav','StressB.wav','StressRef.wav']]
write(sv,lambda i,t:.015*math.sin(2*math.pi*230*t)+(.45*math.sin(2*math.pi*20000*t+.6) if i%12000<12 else 0),3)
write(sb,lambda i,t:.01*math.sin(2*math.pi*90*t)+(.25*math.sin(2*math.pi*20000*t+.6) if i%12000<12 else 0),3)
write(sr,lambda i,t:.2*math.sin(2*math.pi*1000*t)+.08*math.sin(2*math.pi*90*t),3)
command(a.engine,'--mix',sv,sb,sr,r/'Stress',1,'vdbult')
assert json.loads((r/'Stress/Mix.json').read_text())['referenceMix']['confidence']<.5
command(a.engine,'--master',r/'Stress',sr,'MixGlue',r/'Stress/Master')
for m in json.loads((r/'Stress/Master/Master.json').read_text())['masters']:
 il,tp=independent(r/'Stress/Master'/m['file']);assert tp<=-1 and m['metrics']['frames']==144000
 assert abs(tp-m['metrics']['truePeakEstimate'])<.12
 print('PASS independent burst protection',m['file'],tp,'dBTP',flush=True)
if sys.platform=='darwin':
 mono=r/'mono-44100.wav';write(mono,lambda i,t:.08*math.sin(2*math.pi*230*t),5.4,44100,1)
 flac=Path(__file__).parents[1]/'v1/reference-fixture.flac'
 command(a.engine,'--mix',mono,b,flac,r/'Native import',.7,'vdbult')
 command(a.engine,'--master',r/'Native import',flac,'MixGlue',r/'Native import/Master')
 assert json.loads((r/'Native import/Mix.json').read_text())['files'][2]['metrics']['frames']==1152000
 print('PASS native 44.1k mono / stereo mixed-length / FLAC reference / PCM24 render',flush=True)
print('V3 technical integration passed. Synthetic fixtures do not certify professional musical quality.',flush=True)
