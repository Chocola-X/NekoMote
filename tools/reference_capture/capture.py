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
    parser.add_argument('--fixture-references', type=json.loads, default={},
                        help='diagnostic numeric field references within one PSB dictionary')
    parser.add_argument('--trace-math', action='store_true')
    parser.add_argument('--trace-math-step', type=int,
                        help='observe Math calls only during one numbered author update')
    parser.add_argument('--trace-clock', action='store_true',
                        help='observe public Date/performance calls; preserves timestamps')
    parser.add_argument('--trace-integer-stores', action='store_true',
                        help='diagnostic Int32/UInt32 numeric indexed stores, including load')
    parser.add_argument('--trace-integer-index', action='append', type=int, default=[],
                        help='restrict integer indexed stores to explicit diagnostic heap positions')
    parser.add_argument('--trace-integer-stack', default='',
                        help='optional observed caller for integer storage filtering')
    parser.add_argument('--trace-integer-step', type=int,
                        help='optional author step (zero includes initialization)')
    parser.add_argument('--trace-integer-limit', type=int, default=5000)
    parser.add_argument('--fixed-clock-ms', type=int,
                        help='diagnostic deterministic Date.now input for initialization seed')
    parser.add_argument('--trace-random', action='store_true',
                        help='observe public Math.random from initialization; preserves results')
    parser.add_argument('--trace-integer-reads', action='store_true',
                        help='observe bounded numeric heap reads, filtered by caller/step/indices')
    parser.add_argument('--trace-float-stores', action='store_true',
                        help='diagnostic Float32 storage observations; not for benchmarks')
    parser.add_argument('--trace-float-reads', action='store_true',
                        help='observe all numeric Float32 reads; use caller/step filter and bounded limit')
    parser.add_argument('--trace-float-read-index', action='append', type=int, default=[],
                        help='also observe reads at explicit Float32 heap indices; requires stores')
    parser.add_argument('--trace-float-stack', default='',
                        help='optional observed caller name for Float32 storage filtering')
    parser.add_argument('--trace-float-step', type=int,
                        help='optional single author update for Float32 storage observations')
    parser.add_argument('--trace-float-limit', type=int, default=5000,
                        help='bounded diagnostic storage count, 1..100000 (default 5000)')
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
    parser.add_argument('--differences', type=json.loads, default={},
                        help='explicit SetVariableDiff: source -> variable -> {value,frames,easing}')
    parser.add_argument('--physics-scales', type=json.loads, default={'hair': 0, 'parts': 0, 'bust': 0},
                        help='explicit SDK controller scales; zero does not disable integration')
    parser.add_argument('--events', type=json.loads, default=[],
                        help='diagnostic requests before numbered updates: step, variables and/or forces')
    parser.add_argument('--timeline-requests', type=json.loads, default=[],
                        help='ordered SDK timeline blend, fade, play or stop requests after initial plays')
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
    if args.trace_integer_reads and not args.trace_integer_stores:
        parser.error('--trace-integer-reads requires --trace-integer-stores')
    if args.trace_integer_index and (not args.trace_integer_stores or
            len(args.trace_integer_index) > 128 or
            any(i < 0 or i > 0xffffffff for i in args.trace_integer_index)):
        parser.error('integer indices require stores and up to 128 unsigned indices')
    if not 1 <= args.trace_integer_limit <= 100000:
        parser.error('--trace-integer-limit requires 1..100000 records')
    if args.trace_integer_stack and not args.trace_integer_stores:
        parser.error('--trace-integer-stack requires --trace-integer-stores')
    if args.trace_integer_step is not None and (not args.trace_integer_stores or
            not 0 <= args.trace_integer_step <= args.steps):
        parser.error('--trace-integer-step requires stores and an update in 0..steps')
    if args.fixed_clock_ms is not None and not 0 <= args.fixed_clock_ms <= 0xffffffff * 1000:
        parser.error('--fixed-clock-ms requires nonnegative milliseconds within UInt32 seconds')
    if args.trace_math_step is not None and (not args.trace_math or
            not 0 <= args.trace_math_step <= args.steps):
        parser.error('--trace-math-step requires math tracing and an update in 0..steps')
    if args.trace_float_reads and not args.trace_float_stores:
        parser.error('--trace-float-reads requires --trace-float-stores')
    if args.trace_float_stack and not args.trace_float_stores:
        parser.error('--trace-float-stack requires --trace-float-stores')
    if not 1 <= args.trace_float_limit <= 100000:
        parser.error('--trace-float-limit requires 1..100000 records')
    if args.trace_float_step is not None and (not args.trace_float_stores or
            not 0 <= args.trace_float_step <= args.steps):
        parser.error('--trace-float-step requires stores and an update in 0..steps')
    if args.trace_float_read_index and (not args.trace_float_stores or
            len(args.trace_float_read_index) > 128 or
            any(i < 0 or i > 0xffffffff for i in args.trace_float_read_index)):
        parser.error('Float32 reads require stores and up to 128 unsigned indices')
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
    def validate_differences(sources):
        if not isinstance(sources, dict):
            parser.error('differences must be an object')
        for source, variables in sources.items():
            if not source or not isinstance(variables, dict):
                parser.error('difference source requires a nonempty label and variable object')
            for name, r in variables.items():
                if not name or not isinstance(r, dict) or set(r) != {'value', 'frames', 'easing'} or any(not finite_number(v) for v in r.values()) or r['frames'] < 0:
                    parser.error('difference transition requires finite value, frames >= 0 and easing')
    validate_differences(args.differences)
    def validate_timeline_requests(requests):
        if not isinstance(requests, list):
            parser.error('timeline requests must be an array')
        for request in requests:
            if not isinstance(request, dict) or not isinstance(request.get('label'), str) or not request['label']:
                parser.error('timeline requests require a nonempty label')
            kind = request.get('kind')
            fields = {'kind', 'label'}
            if kind == 'play':
                fields.add('flags')
                if type(request.get('flags')) is not int or not 0 <= request['flags'] <= 0xffffffff:
                    parser.error('timeline play requires UInt32 flags')
            elif kind in ('blend', 'fade_in', 'fade_out'):
                fields |= {'frames', 'easing'}
                if not finite_number(request.get('frames')) or request['frames'] < 0 or not finite_number(request.get('easing')):
                    parser.error('timeline transitions require finite frames >= 0 and easing')
                if kind == 'blend':
                    fields |= {'value', 'completion'}
                    if not finite_number(request.get('value')) or request.get('completion') not in ('keep', 'stop'):
                        parser.error('timeline blend requires finite value and keep/stop completion')
            elif kind != 'stop':
                parser.error('unknown timeline request kind')
            if set(request) != fields:
                parser.error('unexpected or missing timeline request fields')
    validate_timeline_requests(args.timeline_requests)
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
    if not isinstance(args.fixture_references, dict) or any(
            not name or not isinstance(value, str) for name, value in args.fixture_references.items()):
        parser.error('field references require paths mapped to paths')
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
        if not isinstance(event, dict) or not {'step'} <= set(event) <= {'step', 'variables', 'forces', 'transitions', 'differences', 'timeline_requests'}:
            parser.error('each event requires step and optional variables/forces/transitions')
        if type(event['step']) is not int or not 1 <= event['step'] <= args.steps:
            parser.error('event step must identify an update in this capture')
        validate_timeline_requests(event.get('timeline_requests', []))
        validate_differences(event.get('differences', {}))
        transitions = event.get('transitions', {})
        if not isinstance(transitions, dict):
            parser.error('event transitions must be an object')
        for name, request in transitions.items():
            if not name or not isinstance(request, dict) or set(request) != {'value', 'frames', 'easing'} or \
                    any(not finite_number(v) for v in request.values()) or request['frames'] < 0:
                parser.error('event transitions require finite value, frames >= 0 and easing')
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
    if args.disable_clamp or args.disable_physics or args.disable_auto_blink or sizes or args.fixture_numbers or args.fixture_strings or args.fixture_references:
        psb, edits = make_fixture(psb, disable_clamp=args.disable_clamp,
                                 disable_physics=args.disable_physics,
                                 disable_auto_blink=args.disable_auto_blink, icon_sizes=sizes,
                                 numbers=args.fixture_numbers, strings=args.fixture_strings,
                                 references=args.fixture_references)
    here = pathlib.Path(__file__).resolve().parent
    html = (here / 'reference.html').read_text()
    if args.fixed_clock_ms is not None:
        html = html.replace('window.fixedClockMs=null;',
                            'window.fixedClockMs=' + str(args.fixed_clock_ms) + ';')
    if args.trace_math:
        html = html.replace('window.enableMathTrace=false;', 'window.enableMathTrace=true;')
    if args.trace_clock:
        html = html.replace('window.enableClockTrace=false;', 'window.enableClockTrace=true;')
    if args.trace_integer_stores:
        html = html.replace('window.enableIntegerStores=false;', 'window.enableIntegerStores=true;')
    if args.trace_random:
        html = html.replace('window.enableRandomTrace=false;', 'window.enableRandomTrace=true;')
    if args.trace_float_stores:
        html = html.replace('window.enableFloatStores=false;', 'window.enableFloatStores=true;')
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
                   traceMathStep=args.trace_math_step,
                   traceFloatStores=args.trace_float_stores, traceFloatStack=args.trace_float_stack,
                   traceFloatStep=args.trace_float_step, traceFloatLimit=args.trace_float_limit,
                   traceFloatReadIndices=args.trace_float_read_index, traceFloatAllReads=args.trace_float_reads,
                   traceRandom=args.trace_random, traceClock=args.trace_clock,
                   fixedClockMs=args.fixed_clock_ms,
                   traceIntegerStores=args.trace_integer_stores, traceIntegerReads=args.trace_integer_reads, traceIntegerStack=args.trace_integer_stack,
                   traceIntegerStep=args.trace_integer_step, traceIntegerLimit=args.trace_integer_limit,
                   traceIntegerIndices=args.trace_integer_index,
                   timelines=list(dict.fromkeys([p['label'] for p in plays] +
                       [r['label'] for r in args.timeline_requests] +
                       [r['label'] for e in args.events for r in e.get('timeline_requests', [])])), plays=plays,
                   timelineFlags=args.timeline_flags, sampleVariables=args.sample_variables,
                   timelineRequests=args.timeline_requests,
                   observeVariables=args.observe_variable, transitions=args.transitions, events=args.events,
                   differences=args.differences,
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
