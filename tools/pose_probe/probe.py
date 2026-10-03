#!/usr/bin/env python3
"""Offline P3 semantic experiment. No production renderer consumes this output."""
import argparse
import json
import math
import pathlib


def value(field, default=None):
    return field['value'] if field['state'] == 2 else default


def identity():
    return [c/3 if axis == 0 else r/3 for r in range(4) for c in range(4) for axis in range(2)]


def mix(a, b, t):
    return [x+(y-x)*t for x, y in zip(a, b)]


def state(content):
    c = value(content, {})
    mesh = value(c.get('mesh', {'state': 0}), {})
    return dict(alive=bool(c), coord=value(c.get('coord', {'state': 0}), [0, 0, 0]),
                angle=value(c.get('angle', {'state': 0}), 0),
                opacity=value(c.get('opa', {'state': 0}), 255)/255,
                src=value(c.get('src', {'state': 0}), ''), icon=value(c.get('icon', {'state': 0}), ''),
                bp=value(mesh.get('bp', {'state': 0}), identity()),
                mesh_present=value(mesh.get('bp', {'state': 0})) is not None)


def sample(layer, motion, variables):
    binding = value(layer['parameterize'], value(motion['parameterize']))
    time = 0
    parameters = layer['parameter'] or motion['parameter']
    if binding is not None:
        p = parameters[binding]
        if p['enabled']:
            var = variables.get(p['id'], 0)
            time = ((min(p['range_end'], max(p['range_begin'], var))-p['range_begin']) *
                    p['division'] / (p['range_end']-p['range_begin'])) if p['range_end'] != p['range_begin'] else 0
    frames = layer['frame_list']
    candidates = [i for i, f in enumerate(frames) if f['time'] <= time]
    if not candidates:
        return state({'state': 0})
    i = candidates[-1]
    a = state(frames[i]['content'])
    if frames[i]['type'] == 3 and i+1 < len(frames) and frames[i+1]['type'] != 0:
        b = state(frames[i+1]['content'])
        t = (time-frames[i]['time'])/(frames[i+1]['time']-frames[i]['time'])
        a['coord'] = mix(a['coord'], b['coord'], t)
        a['angle'] += (b['angle']-a['angle'])*t
        a['opacity'] += (b['opacity']-a['opacity'])*t
        a['bp'] = mix(a['bp'], b['bp'], t)
        a['mesh_present'] |= b['mesh_present']
    return a


def patch(bp, u, v):
    bu = [(1-u)**3, 3*u*(1-u)**2, 3*u*u*(1-u), u**3]
    bv = [(1-v)**3, 3*v*(1-v)**2, 3*v*v*(1-v), v**3]
    return [sum(bu[c]*bv[r]*bp[(r*4+c)*2+axis] for r in range(4) for c in range(4)) for axis in range(2)]


def combined_transforms(transforms):
    # Candidate combine=1 adds normalized control-point displacement. This is
    # tested separately from the model's coupled-variable clamp controls.
    result = []
    neutral = identity()
    for node in transforms:
        node = dict(node, pose=dict(node['pose']))
        node['pose']['bp'] = list(node['pose']['bp'])
        if (node['mesh_combine'] == 1 and result and node['domain'] is not None
                and node['domain'] == result[-1]['domain']
                and node['pose']['coord'] == [0, 0, 0] and node['pose']['angle'] == 0):
            parent = result[-1]['pose']
            parent['bp'] = [a+b-c for a, b, c in zip(parent['bp'], node['pose']['bp'], neutral)]
            parent['mesh_present'] |= node['pose']['mesh_present']
        else:
            result.append(node)
    return result


def transform(transforms, x, y):
    for node in reversed(combined_transforms(transforms)):
        pose, domain = node['pose'], node['domain']
        if pose['mesh_present'] and domain is not None:
            u, v = (x+domain['origin_x'])/domain['width'], (y+domain['origin_y'])/domain['height']
            p = patch(pose['bp'], u, v)
            x, y = p[0]*domain['width']-domain['origin_x'], p[1]*domain['height']-domain['origin_y']
        angle = math.radians(pose['angle'])
        x, y = x*math.cos(angle)-y*math.sin(angle)+pose['coord'][0], x*math.sin(angle)+y*math.cos(angle)+pose['coord'][1]
    return x, y


def apply_clamps(controls, variables):
    result = dict(variables)
    for c in controls:
        if not c['enabled']:
            continue
        x, y = result.get(c['variable_x'], 0), result.get(c['variable_y'], 0)
        if x == 0 and y == 0:
            continue
        if c['type'] != 0 or c['range_begin'] != -c['range_end']:
            raise ValueError('unverified paired clamp: '+c['label'])
        extent = c['range_end']
        x, y = max(-1, min(1, x/extent)), max(-1, min(1, y/extent))
        major, minor = max(abs(x), abs(y)), min(abs(x), abs(y))
        ratio = minor/major
        radius = major + (math.sin(major*math.pi/2)-major)*(1-math.cos(ratio*math.pi/2))
        scale = radius/(major*math.sqrt(1+ratio*ratio))
        result[c['variable_x']], result[c['variable_y']] = x*scale*extent, y*scale*extent
    return result


def inline_rect(icon):
    values = [float(v) for v in icon.split(':')]
    if len(values) != 4:
        raise ValueError('invalid inline domain')
    w, h, ox, oy = values
    return dict(width=w, height=h, origin_x=ox, origin_y=oy)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--asset', type=pathlib.Path, required=True)
    parser.add_argument('--variables', type=json.loads, default={})
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--disable-clamp', action='store_true', help='match diagnostic reference fixture')
    parser.add_argument('--division', type=int, default=8)
    args = parser.parse_args()
    a = json.loads(args.asset.read_text())['model']
    variables = args.variables if args.disable_clamp else apply_clamps(a['paired_clamps'], args.variables)
    motions = {(m['group'], m['name']): m for m in a['parts']}
    sprites = []
    diagnostics = []

    def walk_motion(group, name, ancestors, depth):
        if depth >= 128:
            raise ValueError('cyclic/deep motion references')
        m = motions[group, name]
        for l in m['layers']:
            walk(l, m, ancestors, depth+1)

    def walk(l, m, ancestors, depth):
        s = sample(l, m, variables)
        if not s['alive']:
            return
        r = None
        if s['src'] == 'blank' and ':' in s['icon']:
            r = inline_rect(s['icon'])
        elif s['src'] and (s['src']+'/'+s['icon']) in a['icons']:
            r = a['icons'][s['src']+'/'+s['icon']]
        chain = ancestors + [(l, s, r)]
        if l['type'] == 12:
            diagnostics.append('mask not implemented: '+m['group']+'/'+m['name']+'/'+l['label'])
        if r is not None and s['src'] != 'blank' and l['type'] == 0:
            transforms = [dict(pose=pose, domain=domain, inherit_mask=node['inherit_mask'],
                               mesh_combine=node['mesh_combine']) for node, pose, domain in chain]
            size = args.division if any(pose['mesh_present'] for _, pose, _ in chain) else 1
            vertices = []
            for y in range(size+1):
                for x in range(size+1):
                    u, v = x/size, y/size
                    point = transform(transforms, u*r['width']-r['origin_x'], v*r['height']-r['origin_y'])
                    vertices.append([*point, r['left']+u*r['width'], r['top']+v*r['height']])
            sprites.append(dict(source=s['src']+'/'+s['icon'], label=m['group']+'/'+m['name']+'/'+l['label'],
                                z=sum(pose['coord'][2] for _, pose, _ in chain),
                                opacity=math.prod(pose['opacity'] for _, pose, _ in chain),
                                width=r['width'], height=r['height'], division=size,
                                vertices=vertices, transforms=transforms, chain=[(node['label'],node['inherit_mask'],node['mesh_combine']) for node, _, _ in chain]))
        if s['src'] and (s['src'], s['icon']) in motions:
            walk_motion(s['src'], s['icon'], chain, depth)
        for child in l['children']:
            walk(child, m, chain, depth+1)

    walk_motion(a['base_group'], a['base_motion'], [], 0)
    sprites.sort(key=lambda s: s['z'])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(certified=False, variables=args.variables, evaluated_variables=variables, sprites=sprites,
                                          unsupported=diagnostics), ensure_ascii=False, indent=2)+'\n')
    print(json.dumps(dict(sprites=len(sprites), diagnostics=len(diagnostics))))


if __name__ == '__main__':
    main()
