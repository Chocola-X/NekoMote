"""Exercise shared CPU font initialization with isolated Fontconfig directories."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from xml.sax.saxutils import escape


def run(command, env):
    return subprocess.run(command, env=env, capture_output=True, text=True, timeout=15)


def check(probe, case):
    with tempfile.TemporaryDirectory(prefix="nekomote-font-") as temp:
        root = Path(temp)
        fonts = root / "arbitrary-font-location"
        fonts.mkdir()
        cache = root / "cache"
        cache.mkdir()
        config = root / "fonts.conf"
        # No system includes: the host's usual font directories must not rescue us.
        primary_family = "Noto Sans CJK SC" if case == "cjk" else "Noto Sans"
        fallback_family = "Noto Sans" if case == "cjk" else "NekoMote Test Fallback"
        config.write_text(
            '<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">'
            '<fontconfig><dir>' + escape(str(fonts)) + '</dir><cachedir>' +
            escape(str(cache)) + '</cachedir>'
            '<match target="scan"><test name="file" compare="contains">'
            '<string>preferred.ttf</string></test><edit name="family" mode="assign">'
            '<string>' + primary_family + '</string></edit></match>'
            '<match target="scan"><test name="file" compare="contains">'
            '<string>fallback.ttf</string></test><edit name="family" mode="assign">'
            '<string>' + fallback_family + '</string></edit></match></fontconfig>',
            encoding="utf-8")
        env = dict(os.environ, FONTCONFIG_FILE=str(config), FONTCONFIG_PATH=str(root),
                   XDG_CACHE_HOME=str(root / "xdg-cache"))
        if case != "empty":
            if not shutil.which("fc-match") or not shutil.which("fc-cache"):
                print("SKIP: fontconfig command-line tools unavailable")
                return 77
            source = run(["fc-match", "-f", "%{file}", "sans-serif"], os.environ)
            path = Path(source.stdout)
            if source.returncode or not path.is_file():
                print("SKIP: no system font for relocation integration")
                return 77
            # Verify the integration prerequisite can be rasterized by this build.
            shutil.copyfile(path, fonts / "fallback.ttf")
            result = run([probe], env)
            if result.returncode:
                print("SKIP: system font is unsupported by rasterizer:", result.stderr)
                return 77
            if case == "glyph":
                primary = run([probe, "ก"], env)
                primary_latin = run([probe, "A"], env)
                # Find an installed TrueType face covering Thai, independently
                # from the ordinary Latin face selected above.
                listing = run(["fc-list", "-f", "%{file}\t%{charset}\n"], os.environ)
                other = None
                for line in listing.stdout.splitlines():
                    candidate, charset = line.split("\t", 1)
                    if not candidate.endswith(".ttf"):
                        continue
                    for interval in charset.split():
                        bounds = [int(value, 16) for value in interval.split("-")]
                        if bounds[0] <= 0x0E01 <= bounds[-1]:
                            other = Path(candidate)
                            break
                    if other:
                        break
                if not other:
                    print("SKIP: no complementary Thai TrueType font installed")
                    return 77
                preferred = fonts / "preferred.ttf"
                shutil.copyfile(path, preferred)
                shutil.copyfile(other, fonts / "fallback.ttf")
                preferred.unlink()
                fallback = run([probe, "ก"], env)
                assert fallback.returncode == 0, fallback.stderr
                if primary.stdout == fallback.stdout:
                    print("SKIP: primary font already covers the selected character")
                    return 77
                shutil.copyfile(path, preferred)
                refreshed = run(["fc-cache", "-f", str(fonts)], env)
                assert refreshed.returncode == 0, refreshed.stderr
                selected = run(["fc-match", "-f", "%{file}", "Noto Sans"], env)
                assert selected.stdout == str(preferred), selected.stdout
                combined = run([probe, "ก"], env)
                assert combined.returncode == 0, combined.stderr
                assert combined.stdout == fallback.stdout, (combined.stdout, fallback.stdout)
                combined_latin = run([probe, "A"], env)
                assert combined_latin.stdout == primary_latin.stdout
                latin_inventory = run([probe, "--inventory", "A"], env)
                assert latin_inventory.returncode == 0, latin_inventory.stderr
                assert latin_inventory.stdout.strip() == "1 1 1", latin_inventory.stdout
                thai_inventory = run([probe, "--inventory", "ก"], env)
                assert thai_inventory.returncode == 0, thai_inventory.stderr
                assert thai_inventory.stdout.strip() == "1 2 2", thai_inventory.stdout
            if case == "retry":
                preferred = fonts / "preferred.ttf"
                shutil.copyfile(path, preferred)
                result = run(["fc-cache", "-f", str(fonts)], env)
                assert result.returncode == 0, result.stderr
                selected = run(["fc-match", "-f", "%{file}", "Noto Sans"], env)
                assert selected.stdout == str(preferred), selected.stdout
                # Keep the cached directory intact but make the selected file
                # unreadable by stb. Discovery still returns this candidate first.
                preferred.write_bytes(b"unsupported font data" * 4)
                selected = run(["fc-match", "-f", "%{file}", "Noto Sans"], env)
                assert selected.stdout == str(preferred), selected.stdout
                (fonts / "fallback.ttf").write_bytes(b"unsupported font data" * 4)
                rejected = run([probe], env)
                assert rejected.returncode == 1, rejected.stderr
                assert str(preferred) in rejected.stderr, rejected.stderr
                assert "unsupported font format or face" in rejected.stderr, rejected.stderr
                shutil.copyfile(path, fonts / "fallback.ttf")
        if case == "cjk":
            cjk_source = os.environ.get("NEKOMOTE_TEST_CJK_FONT")
            if not cjk_source:
                cjk_source = run(["fc-match", "-f", "%{file}", "Noto Sans CJK SC"], os.environ).stdout
            if not cjk_source or not Path(cjk_source).is_file():
                print("SKIP: no CJK fixture font")
                return 77
            if not shutil.which("fc-query"):
                print("SKIP: fc-query unavailable for CJK fixture coverage")
                return 77
            coverage = run(["fc-query", "-f", "%{charset}\n", cjk_source], os.environ)
            ranges = []
            for interval in coverage.stdout.split():
                bounds = [int(value, 16) for value in interval.split("-")]
                ranges.append((bounds[0], bounds[-1]))
            sample = "English 简体中文 繁體中文 日本語 あカ汉漢"
            if coverage.returncode or any(
                    not any(lo <= ord(character) <= hi for lo, hi in ranges)
                    for character in sample):
                print("SKIP: CJK fixture does not cover the four-language sample")
                return 77
            shutil.copyfile(cjk_source, fonts / "preferred.ttf")
            refreshed = run(["fc-cache", "-f", str(fonts)], env)
            assert refreshed.returncode == 0, refreshed.stderr
            marker = run([probe, "\U0010ffff"], env)
            assert marker.returncode == 0, marker.stderr
            for character in "A汉漢繁あカ語":
                rendered = run([probe, character], env)
                assert rendered.returncode == 0, rendered.stderr
                assert rendered.stdout.split()[:3] != marker.stdout.split()[:3], character
            inventory = run([probe, "--inventory", "English 简体中文 繁體中文 日本語 あカ"], env)
            assert inventory.returncode == 0, inventory.stderr
            assert inventory.stdout.strip() == "1 1 1", inventory.stdout
        result = run([probe], env)
        if case == "empty":
            assert result.returncode == 1, result
            assert "Fontconfig found no loadable outline font" in result.stderr, result.stderr
        else:
            assert result.returncode == 0, result.stderr
        print("PASS:", case)
        return 0


if __name__ == "__main__":
    sys.exit(check(sys.argv[1], sys.argv[2]))
