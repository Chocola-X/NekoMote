#!/usr/bin/env python3
"""Read-only serving of an external WebGL reference with explicit fixed inputs."""
import argparse
import hashlib
import http.server
import json
import math
import pathlib
import subprocess
import tempfile
import threading
import zipfile
from psb_fixture import make_fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=pathlib.Path, required=True)
    parser.add_argument('--model', required=True)
    parser.add_argument('--browser', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--width', type=int, default=800)
    parser.add_argument('--height', type=int, default=1080)
    parser.add_argument('--x', type=float, default=0)
    parser.add_argument('--y', type=float, default=400)
    parser.add_argument('--opacity', type=float, default=1)
    parser.add_argument('--scale', type=float, default=1)
    parser.add_argument('--variables', type=json.loads, default={})
    parser.add_argument('--isolate-icon', help='diagnostic filtering of original driver draw calls')
    parser.add_argument('--trace', action='store_true')
    parser.add_argument('--trace-mask-pixels', action='store_true', help='diagnostic mask readback; not for benchmarks')
    parser.add_argument('--fixture-icon-size', action='append', default=[],
                        help='diagnostic atlas/icon=width,height crop dimensions')
    parser.add_argument('--fixture-numbers', type=json.loads, default={},
                        help='diagnostic same-width PSB numeric overrides by JSON Pointer')
    parser.add_argument('--fixture-strings', type=json.loads, default={},
                        help='diagnostic same-width references to existing PSB strings by JSON Pointer')
    parser.add_argument('--trace-math', action='store_true')
    parser.add_argument('--disable-clamp', action='store_true',
                        help='diagnostic PSB fixture; original files are never modified')
    parser.add_argument('--disable-physics', action='store_true',
                        help='diagnostic PSB fixture disables bust/hair/parts controller flags')
    parser.add_argument('--disable-auto-blink', action='store_true',
                        help='diagnostic PSB fixture disables automatic eye blinking')
    parser.add_argument('--observe-variable', action='append', default=[],
                        help='observe additional source binding labels omitted by CountVariables')
    parser.add_argument('--transitions', type=json.loads, default={},
                        help='explicit original SetVariable requests: name -> {value,frames,easing}')
    parser.add_argument('--physics-scales', type=json.loads, default={'hair': 0, 'parts': 0, 'bust': 0},
                        help='explicit SDK controller scales; zero does not disable integration')
    parser.add_argument('--events', type=json.loads, default=[],
                        help='diagnostic requests before numbered updates: step, variables and/or forces')
    parser.add_argument('--steps', type=int, default=0)
    parser.add_argument('--step-frames', type=float, default=1)
    parser.add_argument('--timeline', action='append', default=[],
                        help='original timeline label, in play order')
    parser.add_argument('--timeline-flags', type=int, default=0)
    parser.add_argument('--play', action='append', nargs=2, default=[], metavar=('LABEL', 'FLAGS'),
                        help='ordered per-timeline requests; mutually exclusive with --timeline')
    parser.add_argument('--sample-variables', action='store_true',
                        help='record all original variables before and after each fixed update')
    args = parser.parse_args()
    if args.steps < 0 or args.steps > 100000 or not math.isfinite(args.step_frames) or args.step_frames <= 0:
        parser.error('requires 0..100000 steps and a positive finite step size')
    if args.timeline_flags < 0 or args.timeline_flags > 0xffffffff:
        parser.error('timeline flags must be an unsigned 32-bit integer')
    if args.play and args.timeline:
        parser.error('choose either --play requests or --timeline')
    plays = [{'label': name, 'flags': args.timeline_flags} for name in args.timeline]
    for name, flag in args.play:
        value = int(flag, 0)
        if not name or not 0 <= value <= 0xffffffff:
            parser.error('play requires a nonempty label and unsigned 32-bit flags')
        plays.append({'label': name, 'flags': value})
    def finite_number(value):
        return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)
    if not isinstance(args.variables, dict) or any(
            not name or not finite_number(value) for name, value in args.variables.items()):
        parser.error('variables must be an object mapping nonempty labels to finite numbers')
    if not isinstance(args.transitions, dict):
        parser.error('transitions must be an object')
    if not isinstance(args.fixture_numbers, dict) or any(
            not name or not finite_number(value) for name, value in args.fixture_numbers.items()):
        parser.error('numeric fixtures require paths mapped to finite numbers')
    if not isinstance(args.fixture_strings, dict) or any(
            not name or not isinstance(value, str) for name, value in args.fixture_strings.items()):
        parser.error('string fixtures require paths mapped to strings')
    for name, request in args.transitions.items():
        if not name or not isinstance(request, dict) or set(request) != {'value', 'frames', 'easing'}:
            parser.error('each transition requires exactly value, frames and easing')
        if any(not finite_number(value) for value in request.values()) or request['frames'] < 0:
            parser.error('transition fields must be finite numbers with frames >= 0')
    if not isinstance(args.physics_scales, dict) or set(args.physics_scales) != {'hair', 'parts', 'bust'} or \
            any(not finite_number(v) or v < 0 for v in args.physics_scales.values()):
        parser.error('physics scales require finite nonnegative hair, parts and bust')
    if not isinstance(args.events, list):
        parser.error('events must be an array')
    for event in args.events:
        if not isinstance(event, dict) or not {'step'} <= set(event) <= {'step', 'variables', 'forces'}:
            parser.error('each event requires step and optional variables/forces')
        if type(event['step']) is not int or not 1 <= event['step'] <= args.steps:
            parser.error('event step must identify an update in this capture')
        for category in ('variables', 'forces'):
            values = event.get(category, {})
            if not isinstance(values, dict) or any(not name for name in values):
                parser.error('event requests require nonempty labels')
            for value in values.values():
                if category == 'variables' and not finite_number(value) or category == 'forces' and (
                        not isinstance(value, list) or len(value) != 2 or
                        not all(finite_number(v) for v in value)):
                    parser.error('events require finite variable values or two-component force vectors')
    if any(not math.isfinite(value) for value in (args.x, args.y, args.opacity, args.scale)) or \
            args.scale <= 0 or not 0 <= args.opacity <= 1 or \
            not 0 < args.width <= 16384 or not 0 < args.height <= 16384:
        parser.error('invalid finite output dimensions, placement, scale or opacity')
    root = args.reference_root.resolve()
    archive = root / 'data' / (args.model + '.pure.psb.zip')
    with zipfile.ZipFile(archive) as z:
        names = [n for n in z.namelist() if n.endswith('.psb')]
        if len(names) != 1:
            raise ValueError('expected exactly one PSB')
        psb = z.read(names[0])
    original_psb_hash = hashlib.sha256(psb).hexdigest()
    edits = []
    sizes = {}
    for item in args.fixture_icon_size:
        name, dimensions = item.split('=')
        sizes[name] = [float(v) for v in dimensions.split(',')]
        if len(sizes[name]) != 2:
            raise ValueError('fixture icon size requires width,height')
    if args.disable_clamp or args.disable_physics or args.disable_auto_blink or sizes or args.fixture_numbers or args.fixture_strings:
        psb, edits = make_fixture(psb, disable_clamp=args.disable_clamp,
                                 disable_physics=args.disable_physics,
                                 disable_auto_blink=args.disable_auto_blink, icon_sizes=sizes,
                                 numbers=args.fixture_numbers, strings=args.fixture_strings)
    here = pathlib.Path(__file__).resolve().parent
    html = (here / 'reference.html').read_text()
    if args.trace_math:
        html = html.replace('window.enableMathTrace=false;', 'window.enableMathTrace=true;')
    routes = {'/': html.encode(), '/model.psb': psb, '/trace.js': (here / 'trace.js').read_bytes()}
    hashes = {'psb': hashlib.sha256(psb).hexdigest(), 'original_psb': original_psb_hash}
    for name in ('FreeMoteDriver.js', 'emoteplayer.js'):
        data = (root / 'driver' / name).read_bytes()
        routes['/driver/' + name] = data
        hashes[name] = hashlib.sha256(data).hexdigest()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path not in routes:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Type', 'text/html' if self.path == '/' else
                             'text/javascript' if self.path.endswith('.js') else 'application/octet-stream')
            self.end_headers()
            self.wfile.write(routes[self.path])

        def log_message(self, *_):
            pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    options = dict(model=args.model, browser=str(args.browser.resolve()), output=str(args.output.resolve()),
                   width=args.width, height=args.height, x=args.x, y=args.y, scale=args.scale, opacity=args.opacity,
                   variables=args.variables, trace=args.trace, traceMaskPixels=args.trace_mask_pixels, traceMath=args.trace_math, steps=args.steps, stepFrames=args.step_frames,
                   timelines=[p['label'] for p in plays], plays=plays,
                   timelineFlags=args.timeline_flags, sampleVariables=args.sample_variables,
                   observeVariables=args.observe_variable, transitions=args.transitions, events=args.events,
                   url=f'http://127.0.0.1:{server.server_port}/', source_hashes=hashes,
                   physics={**args.physics_scales, 'wind': False},
                   diagnostic_fixture_edits=edits)
    if args.isolate_icon:
        source = root / 'model_conversion' / args.model.split('-')[0] / args.model / (args.model + '.json')
        document = json.loads(source.read_text())
        rect = dict(document['source']['tex']['icon'][args.isolate_icon])
        rect['atlas_height'] = document['source']['tex']['texture']['height']
        options['isolateRect'] = rect
    try:
        with tempfile.NamedTemporaryFile(mode='w', suffix='.json') as config:
            json.dump(options, config)
            config.flush()
            subprocess.run(['node', str(here / 'capture.cjs'), config.name], check=True)
    finally:
        server.shutdown()


if __name__ == '__main__':
    main()
