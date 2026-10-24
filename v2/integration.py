#!/usr/bin/env python3
"""Behavioral integration tests, independent FFmpeg/libsoxr checks. No dependencies beyond Python stdlib and FFmpeg on the build machine."""
from pathlib import Path
import argparse,array,json,math,struct,subprocess,sys,hashlib,wave,re,shutil
p=argparse.ArgumentParser();p.add_argument('engine',type=Path);p.add_argument('legacy',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
a.engine=a.engine.resolve();a.legacy=a.legacy.resolve();r=a.output;r.mkdir(parents=True,exist_ok=False)
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
 q=subprocess.run(['ffmpeg','-hide_banner','-nostats','-i',str(path),'-af','aresample=768000:resampler=soxr:precision=33:cutoff=0.999,astats=metadata=0:reset=0','-f','null','-'],capture_output=True,text=True,check=True)
 peaks=re.findall(r'Peak level dB:\s*(-?[\d.]+)',q.stderr);assert peaks,q.stderr
 peak=float(peaks[-1])
 q=subprocess.run(['ffmpeg','-hide_banner','-nostats','-i',str(path),'-af','ebur128=peak=true','-f','null','-'],capture_output=True,text=True,check=True)
 summary=q.stderr.rsplit('Summary:',1)[-1];lufs=float(re.search(r'I:\s+(-?[\d.]+) LUFS',summary).group(1))
 return lufs,peak

v,b,ref=[r/name for name in ['Vocal.wav','Beat.wav','Reference.wav']]
write(v,lambda i,t:.065*math.sin(2*math.pi*230*t)+.025*math.sin(2*math.pi*2310*t)*(0.5+0.5*math.sin(2*math.pi*t)),5.4)
write(b,lambda i,t:.13*math.sin(2*math.pi*90*t)+.024*math.sin(2*math.pi*7200*t)*math.exp(-25*(t%.25)),6)
write(ref,lambda i,t:.2*math.sin(2*math.pi*1000*t)+.08*math.sin(2*math.pi*90*t)+.03*math.sin(2*math.pi*4200*t),6)
# Set reference near the user's real -8.39 LUFS example; preserve real peak headroom.
command(a.legacy,'--render',v,b,ref,r/'pre-reference')
base=json.loads((r/'pre-reference/Session.json').read_text())['sources'][2]['metrics']['lufs']
factor=10**((-8.39-base)/20)
write(ref,lambda i,t:factor*(.2*math.sin(2*math.pi*1000*t)+.08*math.sin(2*math.pi*90*t)+.03*math.sin(2*math.pi*4200*t)),6)
reference_hash=hashlib.sha256(ref.read_bytes()).hexdigest()
project=r/'Project with spaces ü'
command(a.engine,'--mix',v,b,project,.8)
mix=json.loads((project/'Mix.json').read_text());assert len(mix['files'])==3
assert all(e['metrics']['frames']==288000 for e in mix['files'])
assert mix['unmask']['trace'] and max(mix['unmask']['maximum'])>0
for entry in mix['files']:assert entry['metrics']['spectralWindows']>0 and len(entry['metrics']['bands'])==13
command(a.engine,'--master',project,ref,'MixGlue',project/'Master')
master=json.loads((project/'Master/Master.json').read_text());assert len(master['files'])==7
assert master['selectedMix']=='MixGlue'
for m in master['masters']:
 path=project/'Master'/m['file'];sr,ch,bits,samples=read(path)
 assert (sr,ch,bits)==(48000,2,24) and len(samples)==576000
 assert max(map(abs,samples))<1
 assert abs(master['files'][6]['metrics']['lufs']+8.39)<.03
 assert m['targetLUFS']> -10,'No hidden -12 LUFS target cap'
 il,tp=independent(path)
 assert tp<=-1.0,(m['file'],tp)
 assert abs(il-m['metrics']['lufs'])<.15
 assert abs(tp-m['metrics']['truePeakEstimate'])<.12,(tp,m['metrics']['truePeakEstimate'])
 print('PASS independent PCM24 export:',m['file'],round(il,2),'LUFS,',round(tp,3),'dBTP; target',round(m['targetLUFS'],2),flush=True)
assert reference_hash==hashlib.sha256(ref.read_bytes()).hexdigest()
assert len(master['similarities'])==3 and all('nearIdentical' in pair for pair in master['similarities'])
command(a.legacy,'--render',v,b,ref,r/'V1')
legacy=json.loads((r/'V1/Session.json').read_text())
# The previous vocal-gain rule is retained, without mislabelling V1's static EQ as new unmasking.
assert abs(mix['vocalGainDB']-legacy['vocalGainDB'])<1e-8
print('PASS V1 comparison: retained vocal-gain rule; V1 A',round(legacy['masters'][0]['metrics']['lufs'],2),'LUFS; V2 A',round(master['masters'][0]['metrics']['lufs'],2),'LUFS',flush=True)
# REVIEW accepts the user's selected mix, not an implicit always-Glue path.
command(a.engine,'--master',project,ref,'Balance',project/'Reviewed')
review=json.loads((project/'Reviewed/Master.json').read_text());assert review['selectedMix']=='Balance'
command(a.engine,'--master',project,ref,'MixGlue',project/'Repeated')
for name in ['Master-A.wav','Master-B.wav','Master-C.wav']:
 assert (project/'Master'/name).read_bytes()==(project/'Repeated'/name).read_bytes(),'Deterministic render regression'
command(a.engine,'--mix',v,b,project,.8,ok=False)
command(a.engine,'--mix',v,v,r/'duplicate',.8,ok=False)
command(a.engine,'--mix',v,b,r/'Zero strength',0)
assert (r/'Zero strength/Balance.wav').read_bytes()==(r/'Zero strength/MixGlue.wav').read_bytes()
print('PASS AUTO/REVIEW phases, repeated rendering, zero-strength bypass, source preservation and overwrite/duplicate rejection',flush=True)
# Recreate the exact type of high-frequency short-burst failure from the V1 audit.
sv,sb,sr=[r/name for name in ['StressV.wav','StressB.wav','StressRef.wav']]
write(sv,lambda i,t:.015*math.sin(2*math.pi*230*t)+(.45*math.sin(2*math.pi*20000*t+.6) if i%12000<12 else 0),3)
write(sb,lambda i,t:.01*math.sin(2*math.pi*90*t)+(.25*math.sin(2*math.pi*20000*t+.6) if i%12000<12 else 0),3)
write(sr,lambda i,t:.2*math.sin(2*math.pi*1000*t)+.08*math.sin(2*math.pi*90*t),3)
command(a.legacy,'--render',sv,sb,sr,r/'V1-stress')
old=r/'V1-stress/Master-A.wav';_,old_independent=independent(old);new_measure=meter(old)
assert abs(new_measure['truePeakEstimate']-old_independent)<.06,(new_measure['truePeakEstimate'],old_independent)
print('PASS V1 peak counterexample: V2 reconstructs',round(new_measure['truePeakEstimate'],3),'versus independent',round(old_independent,3),'dBTP',flush=True)
command(a.engine,'--mix',sv,sb,r/'V2-stress',1)
command(a.engine,'--master',r/'V2-stress',sr,'MixGlue',r/'V2-stress/Master')
for m in json.loads((r/'V2-stress/Master/Master.json').read_text())['masters']:
 il,tp=independent(r/'V2-stress/Master'/m['file']);assert tp<=-1.0 and m['metrics']['frames']==144000
 assert abs(tp-m['metrics']['truePeakEstimate'])<.12
 print('PASS high-frequency transient master',m['file'],round(tp,3),'dBTP',flush=True)
# Additional sample phases and spectra; no acceptance based on the engine's own meter alone.
for label,hz,phase in [('12k',12000,math.pi/4),('16k',16000,0),('20k',20000,math.pi/12),('40Hz',40,0),('1k',1000,0)]:
 path=r/(label+'.wav');write(path,lambda i,t,hz=hz,phase=phase:.5*math.sin(2*math.pi*hz*t+phase),2)
 own=meter(path);il,tp=independent(path)
 assert abs(il-own['lufs'])<.15
 assert abs(tp-own['truePeakEstimate'])<.12,(label,tp,own['truePeakEstimate'])
print('PASS independent LUFS/16x-soxr peak phase/frequency test corpus',flush=True)
if sys.platform=='darwin':
 mono=r/'mono-44100.wav';write(mono,lambda i,t:.08*math.sin(2*math.pi*230*t),5.4,44100,1)
 command(a.engine,'--mix',mono,b,r/'native-src',.7)
 native=json.loads((r/'native-src/Mix.json').read_text());assert native['files'][0]['metrics']['frames']==288000
 flac=Path(__file__).parents[1]/'v1/reference-fixture.flac'
 command(a.engine,'--master',r/'native-src',flac,'Balance',r/'native-src/FLAC')
 print('PASS native 44.1k mono import, 48k stereo output, FLAC reference and complete master render',flush=True)
print('V2 behavioral tests passed. Musical quality requires real recordings and listening tests.',flush=True)
