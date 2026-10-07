#!/usr/bin/env node
'use strict';

const { chromium } = require('playwright');

async function main() {
    const args = process.argv.slice(2);
    const opts = {};
    for (let i = 0; i < args.length; i += 2) opts[args[i]] = args[i + 1];

    const url = opts['--url'];
    const width = parseInt(opts['--width'] || '1920');
    const height = parseInt(opts['--height'] || '1080');
    const warmup = parseInt(opts['--warmup'] || '120');
    const measure = parseInt(opts['--measure'] || '300');
    const timeout = parseInt(opts['--timeout'] || '60000');

    if (!url) { console.error('missing --url'); process.exit(2); }

    const browser = await chromium.launch({
        headless: true,
        args: [
            `--window-size=${width},${height}`,
            '--ignore-gpu-blocklist',
            '--use-angle=gl',
            '--enable-webgl',
            '--no-sandbox',
        ],
    });

    const context = await browser.newContext({ viewport: { width, height }, deviceScaleFactor: 1 });
    const page = await context.newPage();

    await page.goto(url, { waitUntil: 'networkidle', timeout });

    try {
        await page.waitForSelector('canvas', { timeout: 15000 });
    } catch (e) {
        console.error('canvas not found');
        await browser.close();
        process.exit(3);
    }

    await page.waitForTimeout(2000);

    const result = await page.evaluate((cfg) => {
        return new Promise((resolve) => {
            const warmup = cfg.warmup, measure = cfg.measure;
            const times = [];
            let last = null, count = 0;
            function loop(t) {
                if (last !== null) {
                    const dt = t - last;
                    if (count >= warmup) times.push(dt);
                    count++;
                }
                last = t;
                if (times.length < measure) {
                    requestAnimationFrame(loop);
                } else {
                    times.sort((a, b) => a - b);
                    const p = (q) => times[Math.min(times.length - 1, Math.floor(times.length * q))];
                    const sum = times.reduce((a, b) => a + b, 0);
                    resolve({
                        frame_times_ms: times,
                        n: times.length,
                        p50_ms: p(0.50),
                        p95_ms: p(0.95),
                        p99_ms: p(0.99),
                        mean_ms: sum / times.length,
                        fps: 1000 / (sum / times.length),
                    });
                }
            }
            requestAnimationFrame(loop);
        });
    }, { warmup, measure });

    console.log(JSON.stringify(result));

    await browser.close();
}

main().catch((e) => { console.error(e.message); process.exit(1); });
