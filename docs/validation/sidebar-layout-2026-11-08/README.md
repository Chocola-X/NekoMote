# 共享菜单布局验证

结论与边界见[修复记录](../../sidebar-layout-fix-2026-11-08.md)。
`cpu-tests.log` / `gpu-tests.log` 是最终修复构建的 CTest 输出。

## CPU 微基准复现

先用 Release preset 构建当前源码。基准不需要显示、素材或音频设备。
以下命令在仓库根目录执行，只把临时编译结果写入 `/tmp`；固定基线为 `46a228f`。
需要当前 Release 构建已存在，且启用了项目默认的固定依赖。

```sh
python3 - <<'PY'
import pathlib
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix='nekomote-sidebar-') as temporary:
    root = pathlib.Path(temporary)
    flags = ['c++', '-std=c++20', '-O3', '-DNDEBUG', '-flto=auto',
             '-fno-fat-lto-objects', '-Iinclude']
    old_objects = []
    for name in ['layout', 'menu']:
        source = root / (name + '.cpp')
        source.write_bytes(subprocess.check_output(
            ['git', 'show', '46a228f:src/ui/' + name + '.cpp']))
        output = root / (name + '.o')
        subprocess.run(flags + ['-c', str(source), '-o', str(output)], check=True)
        old_objects.append(str(output))
    bench = root / 'bench.o'
    subprocess.run(flags + ['-c',
        'docs/validation/sidebar-layout-2026-11-08/bench_sidebar.cpp',
        '-o', str(bench)], check=True)
    libraries = ['-Wl,--start-group'] + [
        'build/release/lib/lib' + name + '.a' for name in
        ['nekomote_ui', 'nekomote_application', 'nekomote_runtime',
         'nekomote_asset_io', 'FLAC', 'nekomote_model', 'nekomote_stb']
    ] + ['-Wl,--end-group', '-lpng', '-lz', '-lfontconfig', '-lm']
    for label, objects in [('baseline', old_objects), ('fixed', [
        'build/release/src/ui/CMakeFiles/nekomote_ui.dir/' + name + '.cpp.o'
        for name in ['layout', 'menu']])]:
        subprocess.run(['c++', '-O3', '-flto=auto', str(bench)] + objects + libraries
                       + ['-o', str(root / label)], check=True)
    for round_index in range(3):
        order = ['baseline', 'fixed'] if round_index % 2 == 0 else ['fixed', 'baseline']
        for label in order:
            print('round=' + str(round_index + 1), label, flush=True)
            subprocess.run([str(root / label)], check=True)
PY
```

分位数是每组 1000 次布局的平均耗时分布，单位纳秒，不是实际呈现帧时间。
计数 Painter 不执行字形绘制或 GPU 工作。分配计数只覆盖测量线程的 C++
`new/new[]`，不代表整个应用、C 分配或设备线程。旧版高度 1064、新版高度 864
是预期的功能差异；本次没有宣称性能收益。
