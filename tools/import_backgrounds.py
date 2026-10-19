#!/usr/bin/env python3
"""从参考仓库导入全量背景图和名称配置到 NekoMote 资产目录。

静态背景（background/*.png）导入到 <dest>/backgrounds/。
动态背景（background/dynamic/*.png）导入到 <dest>/backgrounds/dynamic/。
名称配置（config/background-names.json）复制为 <dest>/backgrounds/background.json。

传入 --recompress 时按迁移同策略优化编码：先剥离 XMP 等元数据块（像素字节
不动），再尝试逐行最优滤波重压缩，仅在结果更小时采用；负收益保持剥离后
的原样。不传时保持原样复制。目标目录已存在的文件会被覆盖。
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys


def import_png(source, destination, tool):
    if tool is None:
        shutil.copy2(source, destination)
        return source.stat().st_size, destination.stat().st_size

    def run(mode, src, dst):
        result = subprocess.run([str(tool), mode, str(src), str(dst)],
                                capture_output=True, text=True, check=True)
        report = json.loads(result.stdout)
        if report.get('ok') is not True:
            raise RuntimeError(f'{src}: {mode} failed: {report.get("error")}')
        return report['source_bytes'], report['output_bytes']

    # Same policy as tools/migrate_png_assets.py: strip metadata first, then
    # adopt a full re-encode only when it is smaller; otherwise keep the
    # stripped file so negative-gain pixel recompression never lands.
    before, after = run('strip', source, destination)
    candidate = destination.with_name(destination.name + '.recompressed')
    try:
        _, recompressed = run('recompress', destination, candidate)
    except subprocess.SubprocessError:
        candidate.unlink(missing_ok=True)
        return before, after
    if recompressed < after:
        candidate.replace(destination)
        return before, recompressed
    candidate.unlink()
    return before, after


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', required=True,
                        help='参考仓库根目录（NekoWebShow）')
    parser.add_argument('--dest', required=True,
                        help='NekoMote 资产根目录（assets/）')
    parser.add_argument('--recompress', type=pathlib.Path, default=None,
                        help='nekomote_png_recompress 工具路径；传入时剥离元数据块，'
                             '并在重压缩结果更小时采用（与迁移策略一致）')
    args = parser.parse_args()

    ref = pathlib.Path(args.reference_root)
    dest = pathlib.Path(args.dest)
    tool = args.recompress.resolve(strict=True) if args.recompress else None
    bg_src = ref / 'background'
    names_src = ref / 'config' / 'background-names.json'
    bg_dest = dest / 'backgrounds'
    dynamic_dest = bg_dest / 'dynamic'

    if not bg_src.is_dir():
        print(f'error: {bg_src} is not a directory', file=sys.stderr)
        return 1

    bg_dest.mkdir(parents=True, exist_ok=True)
    dynamic_dest.mkdir(parents=True, exist_ok=True)

    before_bytes = after_bytes = 0

    static_count = 0
    for f in sorted(bg_src.glob('*.png')):
        b, a = import_png(f, bg_dest / f.name, tool)
        before_bytes += b
        after_bytes += a
        static_count += 1

    dynamic_count = 0
    if (bg_src / 'dynamic').is_dir():
        for f in sorted((bg_src / 'dynamic').glob('*.png')):
            b, a = import_png(f, dynamic_dest / f.name, tool)
            before_bytes += b
            after_bytes += a
            dynamic_count += 1

    names_ok = False
    if names_src.is_file():
        shutil.copy2(names_src, bg_dest / 'background.json')
        names_ok = True

    print(f'static backgrounds: {static_count}')
    print(f'dynamic backgrounds: {dynamic_count} ({dynamic_count // 3} groups)')
    print(f'names config: {"copied" if names_ok else "not found"}')
    if tool is not None:
        print(f'png bytes: {before_bytes} -> {after_bytes} '
              f'({100 * (before_bytes - after_bytes) / before_bytes:.1f}% saved)')
    print(f'total: {static_count + dynamic_count} files')
    return 0


if __name__ == '__main__':
    sys.exit(main())
