#!/usr/bin/env python3
"""Compile project-authored reaction JS and model markers into a native session asset.

The external reference is read only. JS runs only in this offline conversion,
with a bounded VM and no browser/core driver. Only selected voice files are copied.
"""
import argparse
import hashlib
import json
import math
import pathlib
import shutil
import subprocess
import wave

ZONES = ('bust', 'eye', 'face', 'head', 'pant')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=pathlib.Path, required=True)
    parser.add_argument('--model', required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    root = args.reference_root.resolve()
    catalog_path = root / 'config/model-catalog.json'
    catalog = json.loads(catalog_path.read_text())
    if args.model not in catalog:
        parser.error('model is absent from the source catalog')
    entry = catalog[args.model]
    character = entry['character']
    costume = entry.get('costume', args.model)
    model_dir = root / 'model_conversion' / character / args.model
    if not model_dir.exists():
        model_dir = root / 'model_conversion' / character / costume
    source_path = model_dir / (args.model + '.json')
    if not source_path.exists():
        source_path = model_dir / (args.model + '.pure.json')
    metadata = json.loads(source_path.read_text())['metadata']
    markers = dict(metadata.get('charaProfile', {}).get('pixelMarker', {}))
    presentation_path = root / 'config/model-presentation.json'
    presentation = json.loads(presentation_path.read_text()).get(args.model, {})
    markers.update(presentation.get('touchMarkers', {}))
    timelines = {t['label'] for t in metadata.get('timelineControl', [])}
    variables = {v['label'] for v in metadata.get('variableList', [])}
    variables.update(metadata.get('instantVariableList', []))
    # The JS inputs are existing project data, not the proprietary model core.
    config_path = root / 'config' / entry['config']
    library_path = root / 'config/reaction-library.js'
    for path in (config_path, library_path, source_path):
        if not path.resolve().is_relative_to(root):
            parser.error('source path escapes the reference root')
    script = r"""const vm=require('node:vm'),fs=require('node:fs');
const paths=JSON.parse(fs.readFileSync(0,'utf8'));
const context=vm.createContext(Object.create(null),{codeGeneration:{strings:false,wasm:false}});
vm.runInContext(paths.map(p=>fs.readFileSync(p,'utf8')).join('\n'),context,{timeout:1000});
process.stdout.write(vm.runInContext('JSON.stringify(getConfig())',context,{timeout:1000}));"""
    result = subprocess.run(['node', '-e', script], input=json.dumps([str(library_path), str(config_path)]),
                            text=True, capture_output=True, check=True, timeout=5)
    config = json.loads(result.stdout)
    warnings = []

    def action(raw, path):
        main_label = raw.get('mainTimelineLabel', '')
        if main_label and main_label not in timelines:
            warnings.append(path + ': omitted absent main timeline ' + main_label)
            main_label = ''
        slots = raw.get('diffTimelineSlots', [''] * 6)
        if len(slots) != 6:
            raise ValueError(path + ': exactly six difference slots required')
        normalized = []
        for label in slots:
            if label and label not in timelines:
                warnings.append(path + ': omitted absent difference timeline ' + label)
                label = ''
            normalized.append(label)
        requests = []
        for variable in raw.get('variables', []):
            if variable['name'] not in variables:
                warnings.append(path + ': omitted absent variable ' + variable['name'])
                continue
            requests.append(dict(label=variable['name'], target=variable['value'],
                                 author_frames=variable.get('duration', 0) * .06,
                                 easing=variable.get('easing', variable.get('delay', 0))))
        return dict(main_timeline=main_label, difference_slots=normalized, variables=requests)

    def ellipse(x, y, rx, ry):
        return dict(center=[x, y], radius=[rx, ry])

    regions = {zone: None for zone in ZONES}
    eye, mouth = markers.get('eye'), markers.get('mouth')
    if all(isinstance(v, (int, float)) and math.isfinite(v) for v in (eye, mouth)):
        size = max(60, abs(mouth - eye))
        bust = markers.get('bust', mouth + size * 2.5)
        bottom = presentation.get('cutoffY', markers.get('bottom'))
        waist = bust + (bottom - bust) * .55 if bottom is not None else bust + size * 4
        regions.update(eye=ellipse(0, eye, size*.5, size*.35),
                       face=ellipse(0, (eye+mouth)/2, size*.9, size*.8),
                       head=ellipse(0, eye-size*.9, size*1.6, size*1.7),
                       bust=ellipse(0, bust, size*1.3, size),
                       pant=ellipse(0, waist, size*2, size*2.5))
        for zone in ('head', 'bust', 'pant'):
            points = [markers.get(zone + suffix) for suffix in ('AX', 'AY', 'BX', 'BY')]
            if all(isinstance(v, (int, float)) and math.isfinite(v) for v in points):
                ax, ay, bx, by = points
                regions[zone] = ellipse((ax+bx)/2, (ay+by)/2,
                                        max(30, abs(bx-ax)/2), max(30, abs(by-ay)/2))
    else:
        eye = None
        warnings.append('missing finite eye/mouth markers; gaze and touch unavailable')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    reactions = {}
    audio_hashes = {}
    for zone in ZONES:
        for item in config.get(zone, []):
            label = item['id']
            label_path = pathlib.PurePosixPath(label)
            if (not label or label_path.is_absolute() or '..' in label_path.parts
                    or '\\' in label or '\x00' in label):
                raise ValueError('reaction id must be a relative path without traversal')
            if label in reactions:
                reactions[label]['zones'].append(zone)
                continue
            audio_path = ''
            audio_ticks = 0
            if item['reaction'].get('audio'):
                audio_source = (root / item['reaction']['audio']).resolve()
                if not audio_source.is_relative_to(root):
                    raise ValueError(label + ': audio path escapes source root')
                with wave.open(str(audio_source)) as voice:
                    if voice.getcomptype() != 'NONE' or voice.getsampwidth() not in (1, 2, 3, 4):
                        raise ValueError(label + ': unsupported PCM wave format')
                    audio_ticks = math.ceil(voice.getnframes() * 60 / voice.getframerate())
                audio_path = 'audio/' + label_path.name + '.wav'
                destination = args.output.parent / audio_path
                if not destination.resolve().is_relative_to(args.output.parent.resolve()):
                    raise ValueError(label + ': audio destination escapes session directory')
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(audio_source, destination)
                audio_hashes[audio_path] = sha(audio_source)
            duration = max(math.ceil(item['duration'] * .06), audio_ticks)
            beats = []
            for beat in item.get('beats', []):
                tick = math.ceil(beat['at'] * .06)
                if not 0 <= tick < duration:
                    warnings.append(label + ': omitted unreachable beat')
                    continue
                beats.append(dict(author_tick=tick, action=action(beat, label + '/beat')))
            reactions[label] = dict(label=label, zones=[zone], action=action(item['reaction'], label),
                                    recovery=action(item['recovery'], label+'/recovery'), beats=beats,
                                    audio_path=audio_path, duration_ticks=duration,
                                    recovery_ticks=math.ceil(item.get('recoveryDuration', 180) * .06))
    payload = dict(schema='nekomote.session', version=1,
                   session=dict(model=args.model, interaction=dict(eye=eye, regions=regions),
                                idle_timeline='差分用_waiting_loop' if '差分用_waiting_loop' in timelines else '',
                                talk_max=5, reactions=list(reactions.values())))
    args.output.write_text(json.dumps(payload, ensure_ascii=False, indent=None, allow_nan=False, separators=(',', ':'))+'\n')
    report = dict(model=args.model, source_hashes={str(p.relative_to(root)): sha(p) for p in
                  (catalog_path, source_path, presentation_path, library_path, config_path)},
                  audio_hashes=audio_hashes, reactions=len(reactions), warnings=warnings,
                  session_sha256=sha(args.output))
    pathlib.Path(str(args.output)+'.report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()
