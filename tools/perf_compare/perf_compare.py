#!/usr/bin/env python3
"""
NekoMote 原生引擎 vs 原 WebGL 引擎性能对比工具。

控制变量：固定模型、1920x1080、60FPS、预热 120 帧、测量 300 帧。
指标：CPU 占用率、内存 RSS、GPU 利用率、显存占用。
串行测量（避免 GPU 互相干扰），每侧独立采样。

用法：
    python3 tools/perf_compare/perf_compare.py [--model chocola-date-a]
        [--warmup 120] [--measure 300] [--width 1920] [--height 1080]
        [--fps 60] [--output docs/p7-webgl-vs-native-performance.md]
"""

import argparse, json, os, subprocess, sys, time, threading, re, statistics

CLK_TCK = os.sysconf(os.sysconf_names['SC_CLK_TCK'])
PAGE = os.sysconf(os.sysconf_names['SC_PAGE_SIZE'])
NEKOMOTE_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WEBGL_ROOT = os.path.join(os.path.dirname(NEKOMOTE_ROOT), 'NekoWebShow')
PLAYER = os.path.join(NEKOMOTE_ROOT, 'build', 'release', 'bin', 'nekomote_player')
PREVIEW = os.path.join(NEKOMOTE_ROOT, 'build', 'release', 'bin', 'nekomote_preview')
ASSETS = os.path.join(NEKOMOTE_ROOT, 'assets')
WEBGL_MEASURE = os.path.join(NEKOMOTE_ROOT, 'tools', 'perf_compare', 'webgl_measure.cjs')
PW_NODE_MODULES = os.path.join(NEKOMOTE_ROOT, 'tools', 'reference_capture', 'node_modules')


def collect_process_tree(pid):
    pids = [pid]
    try:
        out = subprocess.check_output(
            ['ps', '--ppid', str(pid), '-o', 'pid=', '--no-headers'],
            text=True, stderr=subprocess.DEVNULL).split()
        for child in out:
            pids.extend(collect_process_tree(int(child)))
    except Exception:
        pass
    return pids


def read_proc_cpu_mem(pid):
    try:
        with open(f'/proc/{pid}/stat') as f:
            stat = f.read().split()
        utime = int(stat[13])
        stime = int(stat[14])
        with open(f'/proc/{pid}/statm') as f:
            statm = f.read().split()
        rss = int(statm[1]) * PAGE
        return utime + stime, rss
    except (FileNotFoundError, ProcessLookupError, IndexError):
        return None


def sample_process_tree(pid):
    total_ticks = 0
    total_rss = 0
    found = False
    for p in collect_process_tree(pid):
        r = read_proc_cpu_mem(p)
        if r is not None:
            total_ticks += r[0]
            total_rss += r[1]
            found = True
    return (total_ticks, total_rss) if found else None


def query_gpu():
    try:
        out = subprocess.check_output(
            ['nvidia-smi', '--query-gpu=utilization.gpu,memory.used',
             '--format=csv,noheader,nounits', '-i', '0'],
            text=True, stderr=subprocess.DEVNULL).strip()
        parts = out.split(',')
        return float(parts[0].strip()), float(parts[1].strip())
    except Exception:
        return None


def sample_during(pid, duration_sec, interval_sec=0.1):
    samples = []
    end = time.monotonic() + duration_sec
    prev = sample_process_tree(pid)
    prev_t = time.monotonic()
    while time.monotonic() < end:
        time.sleep(interval_sec)
        now_t = time.monotonic()
        cur = sample_process_tree(pid)
        gpu = query_gpu()
        if cur is not None and prev is not None:
            dt = now_t - prev_t
            cpu_pct = (cur[0] - prev[0]) / CLK_TCK / dt * 100.0 if dt > 0 else 0
            samples.append({
                'cpu_pct': max(0, cpu_pct),
                'rss_mib': cur[1] / 1024 / 1024,
                'gpu_util': gpu[0] if gpu else 0,
                'vram_mib': gpu[1] if gpu else 0,
            })
        prev = cur
        prev_t = now_t
    return samples


def summarize(samples):
    if not samples:
        return {}
    def stats(key):
        vals = sorted(s[key] for s in samples)
        n = len(vals)
        return {
            'mean': statistics.mean(vals),
            'p50': vals[n // 2],
            'p95': vals[min(n - 1, int(n * 0.95))],
            'p99': vals[min(n - 1, int(n * 0.99))],
            'min': vals[0],
            'max': vals[-1],
        }
    return {k: stats(k) for k in ('cpu_pct', 'rss_mib', 'gpu_util', 'vram_mib')}


def measure_native(model, width, height, fps, warmup, measure):
    model_json = os.path.join(ASSETS, 'characters', model, 'model.nkm.json')
    print(f'[native] 帧时间: preview --benchmark {measure} (hidden, vsync=0)')
    print(f'[native] 资源: player {model} {width}x{height} @ {fps}fps 后台采样')

    render_benchmark = None
    preview_cmd = [
        PREVIEW, '--asset', model_json, '--playback',
        '--width', str(width), '--height', str(height),
        '--benchmark', str(measure), '--hidden',
    ]
    preview_proc = subprocess.Popen(preview_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        preview_out = preview_proc.communicate(timeout=(warmup + measure) / fps + 10)[0]
    except subprocess.TimeoutExpired:
        preview_proc.kill()
        preview_out = preview_proc.communicate()[0]
    for line in preview_out.splitlines():
        if 'render+swap CPU ms' in line:
            m = re.search(r'P50=([\d.]+)\s+P95=([\d.]+)\s+P99=([\d.]+);\s+samples=(\d+)', line)
            if m:
                render_benchmark = {'p50_ms': float(m.group(1)), 'p95_ms': float(m.group(2)),
                                    'p99_ms': float(m.group(3)), 'samples': int(m.group(4))}

    measure_duration = (warmup + measure) / fps + 3
    player_cmd = [
        PLAYER, '--assets', ASSETS, '--character', model,
        '--width', str(width), '--height', str(height),
        '--fps', str(fps), '--mute', '--fit', '--no-save',
    ]
    player_proc = subprocess.Popen(player_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    samples = sample_during(player_proc.pid, measure_duration)
    player_proc.terminate()
    try:
        player_proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        player_proc.kill()

    return {
        'render_benchmark': render_benchmark,
        'resource_samples': len(samples),
        'resource_stats': summarize(samples),
    }


def measure_webgl(model, width, height, fps, warmup, measure, port):
    print(f'[webgl] 启动 PHP 服务 :{port} 并测量 {model}')

    index_php = os.path.join(WEBGL_ROOT, 'index.php')
    with open(index_php) as f:
        original_content = f.read()
    modified = False
    if '$local_mode = false;' in original_content:
        with open(index_php, 'w') as f:
            f.write(original_content.replace('$local_mode = false;', '$local_mode = true;', 1))
        modified = True

    php = subprocess.Popen(
        ['php', '-S', f'127.0.0.1:{port}', '-t', WEBGL_ROOT],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    samples = []
    result = {}
    try:
        time.sleep(1.5)
        url = f'http://127.0.0.1:{port}/?char={model}'
        measure_duration = (warmup + measure) / fps + 8

        env = os.environ.copy()
        env['NODE_PATH'] = PW_NODE_MODULES

        node_proc = subprocess.Popen(
            ['node', WEBGL_MEASURE,
             '--url', url, '--width', str(width), '--height', str(height),
             '--warmup', str(warmup), '--measure', str(measure),
             '--timeout', str(int(measure_duration * 1000))],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)

        time.sleep(3)
        browser_pid = None
        for p in collect_process_tree(node_proc.pid):
            try:
                with open(f'/proc/{p}/cmdline', 'rb') as f:
                    if b'chrome' in f.read():
                        browser_pid = p
                        break
            except Exception:
                pass

        samples = []
        if browser_pid:
            print(f'[webgl] chrome_pid={browser_pid}')
            prev = sample_process_tree(browser_pid)
            prev_t = time.monotonic()
            end = time.monotonic() + measure_duration
            while time.monotonic() < end:
                time.sleep(0.1)
                now_t = time.monotonic()
                cur = sample_process_tree(browser_pid)
                gpu = query_gpu()
                if cur is not None and prev is not None:
                    dt = now_t - prev_t
                    cpu_pct = (cur[0] - prev[0]) / CLK_TCK / dt * 100.0 if dt > 0 else 0
                    samples.append({
                        'cpu_pct': max(0, cpu_pct),
                        'rss_mib': cur[1] / 1024 / 1024,
                        'gpu_util': gpu[0] if gpu else 0,
                        'vram_mib': gpu[1] if gpu else 0,
                    })
                prev = cur
                prev_t = now_t
                if node_proc.poll() is not None:
                    break
        else:
            print('[webgl] 警告: 未找到 chrome 进程，跳过资源采样')

        try:
            out = node_proc.stdout.read()
            node_proc.wait(timeout=5)
            for line in out.strip().splitlines():
                try:
                    result = json.loads(line)
                    break
                except json.JSONDecodeError:
                    pass
        except subprocess.TimeoutExpired:
            node_proc.kill()

        return {
            'frame_stats': {k: result.get(k) for k in ('n', 'p50_ms', 'p95_ms', 'p99_ms', 'mean_ms', 'fps')},
            'resource_samples': len(samples),
            'resource_stats': summarize(samples),
        }
    finally:
        php.terminate()
        try:
            php.wait(timeout=3)
        except subprocess.TimeoutExpired:
            php.kill()
        if modified:
            with open(index_php, 'w') as f:
                f.write(original_content)


def res_row(stats, key, unit):
    if not stats or key not in stats:
        return f'- | - | - | - | -'
    s = stats[key]
    return f"{s['mean']:.2f}{unit} | {s['p50']:.2f}{unit} | {s['p95']:.2f}{unit} | {s['min']:.2f}{unit} | {s['max']:.2f}{unit}"


def generate_report(model, width, height, fps, warmup, measure, native, webgl, output_path):
    ts = time.strftime('%Y-%m-%d %H:%M:%S')
    git_commit = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD'],
                                         cwd=NEKOMOTE_ROOT, text=True).strip()
    try:
        gpu_name = subprocess.check_output(
            ['nvidia-smi', '--query-gpu=name', '--format=csv,noheader'],
            text=True).strip()
    except Exception:
        gpu_name = 'N/A'

    n_stats = native.get('resource_stats', {})
    w_stats = webgl.get('resource_stats', {})
    n_render = native.get('render_benchmark', {}) or {}
    w_frame = webgl.get('frame_stats', {}) or {}

    L = []
    L.append('# P7 性能对比：原生引擎 vs WebGL 引擎')
    L.append('')
    L.append(f'**日期**：{ts}  ')
    L.append(f'**提交**：{git_commit}  ')
    L.append(f'**模型**：{model}  ')
    L.append(f'**控制变量**：{width}×{height}，目标 {fps} FPS，预热 {warmup} 帧，测量 {measure} 帧  ')
    L.append(f'**GPU**：{gpu_name}  ')
    L.append(f'**测量方式**：串行执行（避免 GPU 互相干扰），每 100ms 采样进程树 CPU/RSS + nvidia-smi GPU/VRAM')
    L.append('')
    L.append('## 1. 帧时间与 FPS')
    L.append('')
    L.append('| 指标 | 原生 (preview) | WebGL (chromium) |')
    L.append('|---|---|---|')
    L.append(f"| render+swap P50 (ms) | {n_render.get('p50_ms', 'N/A')} | — |")
    L.append(f"| render+swap P95 (ms) | {n_render.get('p95_ms', 'N/A')} | — |")
    L.append(f"| render+swap P99 (ms) | {n_render.get('p99_ms', 'N/A')} | — |")
    L.append(f"| rAF 间隔 P50 (ms) | — | {w_frame.get('p50_ms', 'N/A')} |")
    L.append(f"| rAF 间隔 P95 (ms) | — | {w_frame.get('p95_ms', 'N/A')} |")
    L.append(f"| rAF 间隔 P99 (ms) | — | {w_frame.get('p99_ms', 'N/A')} |")
    L.append(f"| 实测 FPS | — (vsync=0 无上限) | {w_frame.get('fps', 'N/A')} |")
    L.append('')
    L.append('> 原生 render+swap = GL 提交+swapbuffers（vsync=0，不含 CPU 求值 advance/prepare_frame）；'
             'WebGL rAF 间隔 = requestAnimationFrame 间隔（含浏览器合成+vsync）。')
    L.append('> 两者语义不同。原生 CPU 求值基线见 docs/validation/p7-performance-baseline.json'
             '（azuki 425us/帧、fraise 768us/帧）。')
    L.append('> 原生无 vsync 时理论 FPS 上限 ≈ 1000 / (render+swap + cpu_eval)；'
             '真实壁纸场景受 vsync 限制为 60 FPS。')
    L.append('')
    L.append('## 2. CPU 占用率（进程树，%）')
    L.append('')
    L.append('| 引擎 | 平均 | P50 | P95 | 最小 | 最大 |')
    L.append('|---|---|---|---|---|---|')
    L.append(f'| 原生 | {res_row(n_stats, "cpu_pct", "%")}')
    L.append(f'| WebGL | {res_row(w_stats, "cpu_pct", "%")}')
    L.append('')
    L.append('> 进程树 utime+stime 增量 / 采样间隔。原生为单进程；WebGL 含 chromium 主+renderer+GPU process。')
    L.append('')
    L.append('## 3. 内存 RSS（进程树，MiB）')
    L.append('')
    L.append('| 引擎 | 平均 | P50 | P95 | 最小 | 最大 |')
    L.append('|---|---|---|---|---|---|')
    L.append(f'| 原生 | {res_row(n_stats, "rss_mib", "")}')
    L.append(f'| WebGL | {res_row(w_stats, "rss_mib", "")}')
    L.append('')
    L.append('## 4. GPU 利用率（%）')
    L.append('')
    L.append('| 引擎 | 平均 | P50 | P95 | 最小 | 最大 |')
    L.append('|---|---|---|---|---|---|')
    L.append(f'| 原生 | {res_row(n_stats, "gpu_util", "%")}')
    L.append(f'| WebGL | {res_row(w_stats, "gpu_util", "%")}')
    L.append('')
    L.append('## 5. 显存占用（MiB）')
    L.append('')
    L.append('| 引擎 | 平均 | P50 | P95 | 最小 | 最大 |')
    L.append('|---|---|---|---|---|---|')
    L.append(f'| 原生 | {res_row(n_stats, "vram_mib", "")}')
    L.append(f'| WebGL | {res_row(w_stats, "vram_mib", "")}')
    L.append('')
    L.append('> 显存为 nvidia-smi 全局值（单 GPU 串行测量）。基线显存约 772 MiB（桌面环境）。')
    L.append('')
    L.append('## 6. 原始数据')
    L.append('')
    L.append('```json')
    L.append(json.dumps({
        'model': model, 'width': width, 'height': height, 'fps': fps,
        'warmup': warmup, 'measure': measure, 'gpu': gpu_name, 'commit': git_commit,
        'native': native, 'webgl': webgl,
    }, indent=2, default=str))
    L.append('```')
    L.append('')
    L.append('## 7. 局限性')
    L.append('')
    L.append('- 原生 CPU 提交时间与 WebGL rAF 间隔语义不同，不能直接比较绝对值。')
    L.append('- WebGL 在 headless chromium 运行，`--use-angle=gl` 尽量硬件 GL，但仍可能与桌面浏览器有差异。')
    L.append('- 采样间隔 100ms，短时尖峰被平滑；P95/P99 反映采样粒度内尾部分布。')
    L.append('- 串行测量避免 GPU 干扰，但两次运行间系统状态有微小漂移。')
    L.append('- 仅单模型，不同复杂度下比例可能不同。')
    L.append('')

    with open(output_path, 'w') as f:
        f.write('\n'.join(L))
    print(f'[report] 已写入 {output_path}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--model', default='chocola-date-a')
    ap.add_argument('--warmup', type=int, default=120)
    ap.add_argument('--measure', type=int, default=300)
    ap.add_argument('--width', type=int, default=1920)
    ap.add_argument('--height', type=int, default=1080)
    ap.add_argument('--fps', type=int, default=60)
    ap.add_argument('--port', type=int, default=8765)
    ap.add_argument('--output', default=os.path.join(NEKOMOTE_ROOT, 'docs', 'p7-webgl-vs-native-performance.md'))
    args = ap.parse_args()

    if not os.path.exists(PLAYER):
        print(f'ERROR: player 不存在: {PLAYER}', file=sys.stderr); sys.exit(1)
    if not os.path.exists(WEBGL_ROOT):
        print(f'ERROR: 原仓库不存在: {WEBGL_ROOT}', file=sys.stderr); sys.exit(1)

    print(f'=== 性能对比: {args.model} {args.width}x{args.height} @ {args.fps}fps ===\n')

    native = measure_native(args.model, args.width, args.height, args.fps, args.warmup, args.measure)
    print(f'[native] 完成: render_benchmark={native.get("render_benchmark")}')
    print(f'[native] 资源采样 {native.get("resource_samples")} 次\n')

    time.sleep(3)

    webgl = measure_webgl(args.model, args.width, args.height, args.fps, args.warmup, args.measure, args.port)
    print(f'[webgl] 完成: frame_stats={webgl.get("frame_stats")}')
    print(f'[webgl] 资源采样 {webgl.get("resource_samples")} 次\n')

    generate_report(args.model, args.width, args.height, args.fps, args.warmup, args.measure,
                    native, webgl, args.output)


if __name__ == '__main__':
    main()
