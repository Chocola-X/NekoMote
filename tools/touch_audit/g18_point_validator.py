#!/usr/bin/env python3
"""Execute a frozen independent touch request in a fresh evidence directory.

Explicit --probe/--assets/--ledger inputs are required. PNG coordinates become
centered pixels without an extra mirror. Event N=idle_steps+1 dispatches after
exactly idle_steps advances. Current session behavior is tested; visual alignment
and candidate changes are never accepted by this tool.
"""
from __future__ import annotations
import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_touch_audit as audit

ZONES = audit.ZONE_ORDER
CONDITION_STEPS = {c['name']: c['steps'] for c in audit.CONDITIONS}
CONDITION_STEPS['lower_body_diag'] = 0


def validate_spec(spec):
    if not isinstance(spec, dict):
        return ['spec must be an object']
    required = {'model', 'condition', 'png_point', 'width', 'height', 'origin', 'scale',
                'steps', 'seed', 'reaction_seed', 'expected_zone', 'expected_phase_in'}
    errors = [f'missing required field: {k}' for k in sorted(required - spec.keys())]
    if errors:
        return errors
    model = spec['model']
    if not isinstance(model, str) or not model or model in {'.', '..'} or any(c in model for c in '/\\'):
        errors.append('model must be a single nonempty asset directory name')
    condition = spec['condition']
    if not isinstance(condition, str) or condition not in CONDITION_STEPS:
        errors.append('condition unsupported')
    for name in ('png_point', 'origin'):
        if not audit.finite_pair(spec[name]):
            errors.append(f'{name} must contain two finite numbers, excluding bool')
    for name in ('width', 'height'):
        if type(spec[name]) is not int or not 1 <= spec[name] <= 8192:
            errors.append(f'{name} must be an integer in 1..8192')
    scale = spec['scale']
    if isinstance(scale, bool) or not isinstance(scale, (int, float)) or not math.isfinite(scale) or scale <= 0:
        errors.append('scale must be positive and finite, excluding bool')
    for name in ('steps', 'seed', 'reaction_seed'):
        limit = 100000 if name == 'steps' else 0xffffffff
        if type(spec[name]) is not int or not 0 <= spec[name] <= limit:
            errors.append(f'{name} must be an integer in 0..{limit}')
    if isinstance(condition, str) and condition in CONDITION_STEPS and spec['steps'] != CONDITION_STEPS[condition]:
        errors.append('steps differ from declared condition')
    zone = spec['expected_zone']
    if zone is not None and (not isinstance(zone, str) or zone not in ZONES):
        errors.append('expected_zone invalid')
    phases = spec['expected_phase_in']
    if not isinstance(phases, list) or not phases or any(type(p) is not int or p not in (0, 1, 2) for p in phases):
        errors.append('expected_phase_in must be a nonempty list of phases 0/1/2')
    elif len(set(phases)) != len(phases):
        errors.append('expected_phase_in duplicated')
    prefix = spec.get('expected_reaction_prefix')
    if prefix is not None and (not isinstance(prefix, str) or not prefix):
        errors.append('expected_reaction_prefix must be a nonempty string or null')
    return errors


def png_to_centered(x, y, width, height):
    return [x - width / 2, y - height / 2]


def frozen_inputs(spec, assets, probe, ledger):
    data = json.loads(ledger.read_text())
    rows = [m for m in data['models'] if m['model'] == spec['model']]
    if len(rows) != 1 or rows[0].get('status') != 'OK':
        raise ValueError('model unavailable in frozen ledger')
    row = rows[0]
    probe_record = audit.file_record(probe)
    if probe_record['sha256'] != data.get('probe', {}).get('sha256'):
        raise ValueError('probe differs from frozen ledger hash')
    model_dir = (assets / 'characters' / spec['model']).resolve()
    model_dir.relative_to(assets.resolve())
    model_path, session_path = model_dir / 'model.nkm.json', model_dir / 'session.nks.json'
    asset = json.loads(model_path.read_text())['model']
    session = json.loads(session_path.read_text())['session']
    if session['model'] != spec['model'] or type(asset['mirror']) is not bool:
        raise ValueError('invalid model identity/mirror')
    author, reactions = audit.author_regions(session), audit.zone_reactions(session)
    scale = asset['author_scale']
    if type(scale['state']) is not int or scale['state'] not in (0, 1, 2):
        raise ValueError('invalid author_scale state')
    author_scale = scale['value'] if scale['state'] == 2 else 1.0
    if isinstance(author_scale, bool) or not isinstance(author_scale, (int, float)) or not math.isfinite(author_scale) or author_scale <= 0:
        raise ValueError('invalid author scale')
    owned = [(model_path, 'model_asset'), (session_path, 'session_asset')]
    owned += [(model_dir / t['path'], 'texture_asset') for t in asset['textures']]
    records = []
    for path, role in owned:
        path.resolve().relative_to(assets.resolve())
        record = {**audit.file_record(path), 'role': role}
        expected = [r for r in row.get('assets', []) if r.get('role') == role and Path(r['path']).name == path.name]
        if len(expected) != 1 or record['sha256'] != expected[0]['sha256']:
            raise ValueError(f'{role} differs from frozen ledger hash')
        records.append(record)
    if spec['condition'] != 'lower_body_diag':
        conditions = [c for c in row['conditions'] if c['name'] == spec['condition']]
        if len(conditions) != 1 or conditions[0].get('pass') is not True:
            raise ValueError('condition unsuccessful/missing in frozen ledger')
        params = conditions[0]['params']
        for key, field in (('steps', 'steps'), ('origin', 'origin'), ('scale', 'scale'), ('seed', 'random_seed'), ('reaction_seed', 'reaction_seed')):
            if spec[key] != params[field]:
                raise ValueError(f'{key} differs from frozen condition')
    projected = audit.projected_regions(author, reactions, asset['mirror'], author_scale, spec)
    return model_path, session_path, asset['mirror'], reactions, projected, records + [probe_record, audit.file_record(ledger), audit.file_record(Path(__file__)), audit.file_record(Path(audit.__file__))]


def validate_point(spec, output_dir, *, probe, assets, ledger):
    output_dir.mkdir(parents=True, exist_ok=False)
    result = {'spec': spec, 'validated': False, 'ok': False, 'errors': validate_spec(spec),
              'scope': 'Current session point behavior only; candidates remain pending.'}
    if result['errors']:
        try:
            json.dumps(spec, allow_nan=False)
        except (ValueError, TypeError):
            result['spec'] = repr(spec)
    try:
        if result['errors']:
            return result
        model, session, mirror, reactions, projected, frozen = frozen_inputs(spec, assets, probe, ledger)
        result['frozen_inputs'] = frozen
        point = png_to_centered(*spec['png_point'], spec['width'], spec['height'])
        step = spec['steps'] + 1
        request = {'steps': step, 'random_seed': spec['seed'], 'reaction_seed': spec['reaction_seed'],
                   'origin': spec['origin'], 'scale': spec['scale'],
                   'events': [{'step': step, 'kind': 'touch', 'point': point}]}
        input_path, response_path = output_dir / 'request.json', output_dir / 'response.json'
        audit.write_json(input_path, request)
        request_record = audit.file_record(input_path)
        result.update(centered_point=point, event_step=step, mirror=mirror, request=request_record)
        process = audit.run_process([probe, model, session, input_path, response_path], output_dir / 'probe', timeout=30)
        result['probe'] = process
        for record in frozen + [request_record]:
            if audit.sha256_file(record['path']) != record['sha256']:
                result['errors'].append(f"frozen input changed during execution: {record['path']}")
        if response_path.exists():
            result['response'] = audit.file_record(response_path)
        if not process['ok']:
            result['errors'].append('probe failed/Busy/timeout; never a measured miss')
            return result
        data = json.loads(response_path.read_text())
        audit.validate_probe(data, step)
        if type(data.get('steps')) is not int or data['steps'] != step:
            result['errors'].append('response step count differs from request')
        if data['presentation_mirror'] != mirror:
            result['errors'].append('response mirror differs from model')
        actual_regions = data['touch_regions']
        for zone in ZONES:
            expected, actual = projected[zone], actual_regions[zone]
            if (expected is None) != (actual is None) or (expected is not None and any(abs(actual[k][i] - expected[k][i]) > 0.001 for k in ('center', 'radius') for i in (0, 1))):
                result['errors'].append(f'response projected region differs: {zone}')
        if any(s['phase'] != 0 or s['reaction'] != '' for s in data['samples'][:step]):
            result['errors'].append('independent initial state not idle before touch')
        zone = audit.expected_zone(point, projected)
        if zone != spec['expected_zone']:
            result['errors'].append('expected_zone differs from geometric hit priority')
        sample = data['samples'][step]
        phase, reaction = sample['phase'], sample['reaction']
        result.update(actual_zone=zone, actual_phase=phase, actual_reaction=reaction, touch_regions=actual_regions)
        if zone is None:
            if phase != 0 or reaction != '':
                result['errors'].append('miss requires idle phase and empty reaction')
        elif phase not in (1, 2) or reaction not in reactions[zone]:
            result['errors'].append("response phase/reaction differs from selected zone's allowed reactions")
        if phase not in spec['expected_phase_in']:
            result['errors'].append('phase differs from expected_phase_in')
        prefix = spec.get('expected_reaction_prefix')
        if prefix is not None and not reaction.startswith(prefix):
            result['errors'].append('reaction differs from additional prefix constraint')
        result['validated'] = True
        result['ok'] = not result['errors']
        return result
    except (ValueError, OSError, KeyError, TypeError, OverflowError) as error:
        result['errors'].append(str(error))
        return result
    finally:
        audit.write_json(output_dir / 'result.json', result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('spec', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--ledger', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = validate_point(json.loads(args.spec.read_text()), args.output,
                                probe=args.probe, assets=args.assets, ledger=args.ledger)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    print(json.dumps({'ok': result['ok'], 'validated': result['validated'], 'errors': result['errors']}))
    return 0 if result['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
