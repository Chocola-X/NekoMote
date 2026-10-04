#!/usr/bin/env python3
"""Compare straight RGBA PNGs and emit numerical and visual regression evidence."""
import argparse
import hashlib
import json
import pathlib
from PIL import Image, ImageChops


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('reference', type=pathlib.Path)
    p.add_argument('native', type=pathlib.Path)
    p.add_argument('--output', type=pathlib.Path, required=True)
    p.add_argument('--tolerance', type=int, default=2)
    args = p.parse_args()
    r = Image.open(args.reference).convert('RGBA')
    n = Image.open(args.native).convert('RGBA')
    if r.size != n.size or r.getchannel('A').getbbox() is None or n.getchannel('A').getbbox() is None:
        raise ValueError('reference/native dimensions disagree or image is empty')
    diff = ImageChops.difference(r, n)
    maximum = [high for _, high in diff.getextrema()]
    # Canvas and GL both quantize associated colour before PNG unassociation.
    # Very low alpha can magnify a 1/255 associated error to 255/255 in straight
    # RGB. Compare alpha and actual composition on both black and white, and
    # retain raw maxima for inspection instead of hiding this distinction.
    alpha_error = ImageChops.difference(r.getchannel('A'), n.getchannel('A')).getextrema()[1]
    visual_diffs = []
    for bg in ((0, 0, 0, 255), (255, 255, 255, 255)):
        background = Image.new('RGBA', r.size, bg)
        visual_diffs.append(ImageChops.difference(Image.alpha_composite(background, r), Image.alpha_composite(background, n)))
    visual_maxima = [[high for _, high in image.getextrema()][:3] for image in visual_diffs]
    errors = sum(max(a, b, c) > args.tolerance for a, b, c in zip(
        (max(v[:3]) for v in visual_diffs[0].getdata()),
        (max(v[:3]) for v in visual_diffs[1].getdata()),
        ImageChops.difference(r.getchannel('A'), n.getchannel('A')).getdata()))
    report = dict(reference_sha256=hashlib.sha256(args.reference.read_bytes()).hexdigest(),
                  native_sha256=hashlib.sha256(args.native.read_bytes()).hexdigest(),
                  size=r.size, raw_max_rgba=maximum, alpha_max_error=alpha_error,
                  composed_max_rgb_black_white=visual_maxima, tolerance=args.tolerance,
                  pixels_over_tolerance=errors, reference_bbox=r.getbbox(), native_bbox=n.getbbox(),
                  passed=errors == 0)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(str(args.output) + '.json').write_text(json.dumps(report, indent=2) + '\n')
    bbox = r.getbbox()
    box = (max(0, bbox[0]-8), max(0, bbox[1]-8), min(r.width, bbox[2]+8), min(r.height, bbox[3]+8))
    crops = [image.crop(box) for image in (r, n)]
    background = Image.new('RGBA', crops[0].size, (80, 80, 80, 255))
    panels = [Image.alpha_composite(background, crop).convert('RGB') for crop in crops]
    panels.append(visual_diffs[0].crop(box).convert('RGB').point(lambda v: min(255, v*16)))
    out = Image.new('RGB', (panels[0].width*3, panels[0].height))
    for i, panel in enumerate(panels):
        out.paste(panel, (i*panel.width, 0))
    if out.height < 128:
        out = out.resize((out.width*4, out.height*4), Image.Resampling.NEAREST)
    out.save(pathlib.Path(str(args.output) + '.png'))
    print(json.dumps(report))
    if errors:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
