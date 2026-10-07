const {chromium} = require('playwright');
const {spawn} = require('child_process');

(async () => {
  const fs = require('fs');
  const path = require('path');
  const outDir = path.resolve(__dirname, '../../build/evidence/v6/web_ui');
  fs.mkdirSync(outDir, {recursive: true});

  const php = spawn('php', ['-S', '127.0.0.1:8125', '-t', '/run/media/chocola/DATA/Project/NekoWebShow']);
  await new Promise(r => setTimeout(r, 1500));
  try {
    const browser = await chromium.launch({
      executablePath: '/home/chocola/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell',
      headless: true,
      args: ['--enable-webgl', '--ignore-gpu-blocklist', '--use-angle=gl']
    });
    const page = await browser.newPage({viewport: {width: 800, height: 1080}});
    await page.goto('http://127.0.0.1:8125/index.php?char=chocola-date-a');
    await page.waitForTimeout(3000);
    
    // State 1: Closed UI with toggle icon visible
    await page.screenshot({path: path.join(outDir, 'closed.png')});
    console.log('Saved web_ui closed.png');

    // Click toggle icon to open sidebar
    await page.click('#toggle-icon');
    await page.waitForTimeout(600);

    // State 2: Open with Character (Chocola) drawer expanded
    const menuItems = await page.$$('.menu-item');
    if (menuItems.length > 0) {
      await menuItems[0].click();
      await page.waitForTimeout(600);
      await page.screenshot({path: path.join(outDir, 'open-character.png')});
      console.log('Saved web_ui open-character.png');
    }

    // State 3: Open with Vanilla drawer expanded
    if (menuItems.length > 1) {
      await menuItems[1].click();
      await page.waitForTimeout(600);
      await page.screenshot({path: path.join(outDir, 'open-vanilla.png')});
      console.log('Saved web_ui open-vanilla.png');
    }

    // State 4: Open with Settings drawer expanded
    const settingsItem = await page.$('.settings-item');
    if (settingsItem) {
      await settingsItem.click();
      await page.waitForTimeout(600);
      await page.screenshot({path: path.join(outDir, 'open-settings.png')});
      console.log('Saved web_ui open-settings.png');
    }

    await browser.close();
  } finally {
    php.kill();
  }
})();
