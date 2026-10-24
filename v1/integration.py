#!/usr/bin/env python3
"""Deterministic synthetic end-to-end tests; standard library only.
No musical-quality claim. Optional independent FFmpeg meter comparison.
"""
import argparse
import array
import hashlib
import json
import math
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import wave

p = argparse.ArgumentParser()
p.add_argument('engine', type=pathlib.Path)
p.add_argument('output', type=pathlib.Path)
p.add_argument('--independent', action='store_true')
a = p.parse_args()
a.engine = a.engine.resolve()
a.output.mkdir(parents=True, exist_ok=True)


def fixture(path, seconds, kind, sample_rate=48000, channels=2):
    data = array.array('h')
    for i in range(round(seconds * sample_rate)):
        t = i / sample_rate
        if kind == 'vocal':
            env = (0.35 + 0.65 * max(0, math.sin(2 * math.pi * 1.7 * t))) if t > .25 else 0
            left = env * (.075 * math.sin(2 * math.pi * 230 * t) + .04 * math.sin(2 * math.pi * 2310 * t))
            right = left * .98 + .003 * math.sin(2 * math.pi * 840 * t) * env
        elif kind == 'beat':
            kick = math.exp(-25 * (t % .5))
            left = .18 * kick * math.sin(2 * math.pi * 65 * t) + .07 * math.sin(2 * math.pi * 110 * t)
            left += .035 * math.sin(2 * math.pi * 3200 * t) * math.exp(-35 * (t % .25))
            right = left + .02 * math.sin(2 * math.pi * 7200 * t)
        elif kind == 'reference':
            left = .12 * math.sin(2 * math.pi * 95 * t) + .08 * math.sin(2 * math.pi * 1200 * t) + .025 * math.sin(2 * math.pi * 6800 * t)
            right = left + .015 * math.sin(2 * math.pi * 4100 * t)
        elif kind == 'stress':
            left = .02 * math.sin(2 * math.pi * 1000 * t)
            if i % 9600 == 9598:
                left = .99
            elif i % 9600 == 9599:
                left = -.99
            right = left
        else:
            left = right = 0
        values = [left, right] if channels == 2 else [left] * channels
        data.extend(max(-32768, min(32767, round(v * 32768))) for v in values)
    if sys.byteorder != 'little':
        data.byteswap()
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(data.tobytes())


def run(args, success=True):
    result = subprocess.run([str(a.engine)] + [str(x) for x in args], capture_output=True, text=True, timeout=240)
    if (result.returncode == 0) != success:
        raise AssertionError(f'Unexpected exit {result.returncode}\n{result.stdout}\n{result.stderr}')
    return result


def pcm(path):
    with wave.open(str(path), 'rb') as w:
        assert w.getnchannels() == 2 and w.getsampwidth() == 3 and w.getframerate() == 48000
        frames = w.getnframes()
        raw = w.readframes(frames)
    samples = []
    for i in range(0, len(raw), 3):
        v = int.from_bytes(raw[i:i+3], 'little', signed=True)
        samples.append(v / 8388608)
    return frames, samples


vocal, beat, ref = [a.output / x for x in ('VOCAL.wav', 'BEAT.wav', 'REFERENCE.wav')]
fixture(vocal, 5.4, 'vocal')
fixture(beat, 6.0, 'beat')
fixture(ref, 6.2, 'reference')
source_hashes = [hashlib.sha256(f.read_bytes()).hexdigest() for f in (vocal, beat, ref)]
project = a.output / 'Project ü with spaces'
assert not project.exists(), 'Use a fresh output directory'
run(['--render', vocal, beat, ref, project])
session = json.loads((project / 'Session.json').read_text())
assert len(session['masters']) == 3
assert session['sources'][0]['metrics']['leadingSilenceSeconds'] >= .25
assert any('Längen' in text for text in session['warnings'])
assert session['original']['frames'] == 288000
assert session['balanced']['frames'] == 288000
hashes = []
for master in session['masters']:
    path = project / master['file']
    frames, samples = pcm(path)
    assert frames == 288000, 'No truncation of longer stem'
    assert max(abs(x) for x in samples) < 1
    assert master['metrics']['clippedFrames'] == 0
    assert master['metrics']['truePeakEstimate'] <= -1.0
    assert math.isfinite(master['metrics']['lufs'])
    hashes.append(hashlib.sha256(path.read_bytes()).hexdigest())
    if a.independent:
        assert shutil.which('ffmpeg'), 'Independent meter requested but ffmpeg missing'
        result = subprocess.run(['ffmpeg', '-hide_banner', '-nostats', '-i', str(path), '-filter_complex',
                                 'ebur128=peak=true', '-f', 'null', '-'], capture_output=True, text=True, check=True)
        summary = result.stderr.rsplit('Summary:', 1)[-1]
        integrated = float(re.search(r'I:\s+(-?[\d.]+) LUFS', summary).group(1))
        peak = float(re.search(r'Peak:\s+(-?[\d.]+) dBFS', summary).group(1))
        assert abs(integrated - master['metrics']['lufs']) < .2
        assert abs(peak - master['metrics']['truePeakEstimate']) < .35
        print(f'INDEPENDENT {master["file"]}: FFmpeg {integrated:.1f} LUFS / {peak:.1f} dBTP; engine {master["metrics"]["lufs"]:.3f} / {master["metrics"]["truePeakEstimate"]:.3f}')
assert len(set(hashes)) == 3, 'Three actual different renders'
assert source_hashes == [hashlib.sha256(f.read_bytes()).hexdigest() for f in (vocal, beat, ref)]
run(['--render', vocal, beat, ref, project], success=False)
run(['--render', vocal, vocal, ref, a.output / 'duplicate'], success=False)
silent = a.output / 'silence.wav'
fixture(silent, 1, 'silence')
run(['--render', silent, beat, ref, a.output / 'silent-project'], success=False)
assert not (a.output / 'silent-project' / 'Session.json').exists()
stress_v, stress_b = a.output / 'stress-v.wav', a.output / 'stress-b.wav'
fixture(stress_v, 4, 'stress')
fixture(stress_b, 4, 'stress')
run(['--render', stress_v, stress_b, ref, a.output / 'stress-project'])
stress_report = json.loads((a.output / 'stress-project' / 'Session.json').read_text())
assert all(m['maxLimiterReductionDB'] > .5 for m in stress_report['masters']), 'Exercise actual limiter reduction'
assert all(m['metrics']['truePeakEstimate'] <= -1 and m['metrics']['clippedFrames'] == 0 for m in stress_report['masters'])
assert all(m['metrics']['frames'] == 192000 for m in stress_report['masters'])
# Check the known stereo-to-mono arithmetic on the actual export.
_, stereo_samples = pcm(project / 'Master-A.wav')
raw = (project / 'Mono-Master-A.wav').read_bytes()
offset = 12
while raw[offset:offset+4] != b'data':
    size = struct.unpack_from('<I', raw, offset+4)[0]
    offset += 8 + size + (size & 1)
size = struct.unpack_from('<I', raw, offset+4)[0]
mono_samples = struct.unpack('<' + 'f' * (size//4), raw[offset+8:offset+8+size])
assert len(mono_samples) == len(stereo_samples)
assert max(abs(mono_samples[i] - (stereo_samples[i] + stereo_samples[i+1]) / 2) for i in range(0, len(mono_samples), 2)) < 1e-7
bad = a.output / 'broken.wav'
bad.write_bytes(b'RIFF\x10\x00\x00\x00WAVE')
run(['--inspect', bad], success=False)

if sys.platform == 'darwin':
    # Exercise the actual Apple decoder and SRC path, including mono input.
    mono = a.output / 'mono-44100.wav'
    fixture(mono, 5.4, 'vocal', sample_rate=44100, channels=1)
    info = json.loads(run(['--inspect', mono]).stdout)
    assert info['channels'] == 1 and info['sampleRate'] == 44100
    run(['--render', mono, beat, ref, a.output / 'converted'])
    converted = json.loads((a.output / 'converted' / 'Session.json').read_text())
    assert converted['original']['frames'] == 288000
    multi = a.output / 'multichannel.wav'
    fixture(multi, 1, 'beat', channels=4)
    run(['--inspect', multi], success=False)
    # Small genuine FLAC fixture is included; Core Audio must decode it.
    flac = pathlib.Path(__file__).with_name('reference-fixture.flac')
    info = json.loads(run(['--inspect', flac]).stdout)
    assert info['channels'] == 2
    run(['--render', vocal, beat, flac, a.output / 'flac-project'])
    print('PASS native Apple SRC 44.1k mono -> 48k stereo, multichannel rejection and FLAC decode/render')

print('PASS synthetic end-to-end: 3 distinct PCM24 masters, length, peak checks, source preservation, duplicate/overwrite/silence/corruption rejection')
print('PASS high-crest limiter stress, exported peak ceiling, exact L/R mono arithmetic')
print('No listening validation or professional-quality certification has occurred.')
