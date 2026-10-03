#!/usr/bin/env python3
"""Read-only serving of an external WebGL reference with explicit fixed inputs."""
import argparse
import hashlib
import http.server
import json
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
    parser.add_argument('--trace-math', action='store_true')
    parser.add_argument('--disable-clamp', action='store_true',
                        help='diagnostic PSB fixture; original files are never modified')
    parser.add_argument('--steps', type=int, default=0)
    parser.add_argument('--step-frames', type=float, default=1)
    args = parser.parse_args()
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
    if args.disable_clamp or sizes:
        psb, edits = make_fixture(psb, disable_clamp=args.disable_clamp, icon_sizes=sizes)
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
                   url=f'http://127.0.0.1:{server.server_port}/', source_hashes=hashes,
                   physics={'hair': 0, 'parts': 0, 'bust': 0, 'wind': False},
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
