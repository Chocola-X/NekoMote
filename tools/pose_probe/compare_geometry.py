#!/usr/bin/env python3
"""Evaluate candidate formulas at original WebGL UV/vertex samples, never replay them."""
import argparse
import hashlib
import json
import math
import pathlib
from probe import transform


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--asset', type=pathlib.Path, required=True)
    p.add_argument('--probe', type=pathlib.Path, required=True)
    p.add_argument('--trace', type=pathlib.Path, required=True)
    p.add_argument('--output', type=pathlib.Path, required=True)
    args = p.parse_args()
    a = json.loads(args.asset.read_text())['model']
    probe = json.loads(args.probe.read_text())
    metadata = json.loads(args.trace.with_name(args.trace.name.removesuffix('.trace.json')+'.json').read_text())
    draws = json.loads(args.trace.read_text())
    results = []
    missing = []
    for s in probe['sprites']:
        if s['opacity'] <= 0:
            continue
        r = a['icons'][s['source']]
        errors = []
        samples = []
        tex = next(t for t in a['textures'] if t['name'] == r['texture'])
        for draw in draws:
            if draw.get('texture') != 2:
                continue
            attrs = draw['attributes']
            for i in set(draw['indices']):
                u, v = attrs['a_texCoord']['data'][i][:2]
                v = tex['height']-v
                if not (r['left']-0.001 <= u <= r['left']+r['width']+0.001 and
                        r['top']-0.001 <= v <= r['top']+r['height']+0.001):
                    continue
                x, y = transform(s['transforms'], u-r['left']-r['origin_x'], v-r['top']-r['origin_y'])
                expected = [x*metadata['scale']+metadata['x'], y*metadata['scale']+metadata['y']]
                actual = attrs['a_pos']['data'][i][:2]
                error = math.dist(expected, actual)
                errors.append(error)
                if error > 0.1 and len(samples) < 3:
                    samples.append(dict(uv=[u, v], native_candidate=expected, reference=actual, error=error))
        if not errors:
            missing.append(s["label"] + ": " + s["source"])
        if errors:
            results.append(dict(source=s['source'], label=s['label'], samples=len(errors),
                                max_error=max(errors), rms_error=math.sqrt(sum(e*e for e in errors)/len(errors)),
                                mismatches=samples))
    report = dict(certified=False, variables=probe['variables'], comparisons=results,
                  missing_primitives=missing,
                  hashes={name: hashlib.sha256(path.read_bytes()).hexdigest()
                          for name, path in [('asset', args.asset), ('probe', args.probe), ('trace', args.trace)]},
                  max_error=max((r['max_error'] for r in results), default=None),
                  unsupported=probe['unsupported'])
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps(dict(max_error=report['max_error'], primitives=len(results))))


if __name__ == '__main__':
    main()
