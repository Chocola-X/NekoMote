#!/usr/bin/env python3
"""从参考仓库导入全量背景图和名称配置到 NekoMote 资产目录。

静态背景（background/*.png）复制到 <dest>/backgrounds/。
动态背景（background/dynamic/*.png）复制到 <dest>/backgrounds/dynamic/。
名称配置（config/background-names.json）复制到 <dest>/backgrounds/background.json。

目标目录已存在的文件会被覆盖；不存在的子目录自动创建。
"""
import argparse
import pathlib
import shutil
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', required=True,
                        help='参考仓库根目录（NekoWebShow）')
    parser.add_argument('--dest', required=True,
                        help='NekoMote 资产根目录（assets/）')
    args = parser.parse_args()

    ref = pathlib.Path(args.reference_root)
    dest = pathlib.Path(args.dest)
    bg_src = ref / 'background'
    names_src = ref / 'config' / 'background-names.json'
    bg_dest = dest / 'backgrounds'
    dynamic_dest = bg_dest / 'dynamic'

    if not bg_src.is_dir():
        print(f'error: {bg_src} is not a directory', file=sys.stderr)
        return 1

    bg_dest.mkdir(parents=True, exist_ok=True)
    dynamic_dest.mkdir(parents=True, exist_ok=True)

    static_count = 0
    for f in sorted(bg_src.glob('*.png')):
        shutil.copy2(f, bg_dest / f.name)
        static_count += 1

    dynamic_count = 0
    if (bg_src / 'dynamic').is_dir():
        for f in sorted((bg_src / 'dynamic').glob('*.png')):
            shutil.copy2(f, dynamic_dest / f.name)
            dynamic_count += 1

    names_ok = False
    if names_src.is_file():
        shutil.copy2(names_src, bg_dest / 'background.json')
        names_ok = True

    print(f'static backgrounds: {static_count}')
    print(f'dynamic backgrounds: {dynamic_count} ({dynamic_count // 3} groups)')
    print(f'names config: {"copied" if names_ok else "not found"}')
    print(f'total: {static_count + dynamic_count} files')
    return 0


if __name__ == '__main__':
    sys.exit(main())
