const {chromium} = require('playwright');
const {spawn} = require('child_process');

(async () => {
  const php = spawn('php', ['-S', '127.0.0.1:8123', '-t', '/run/media/chocola/DATA/Project/NekoWebShow']);
  await new Promise(r => setTimeout(r, 1500));
  try {
    const browser = await chromium.launch({
      executablePath: '/home/chocola/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell',
      headless: true,
      args: ['--enable-webgl', '--ignore-gpu-blocklist', '--use-angle=gl']
    });
    const page = await browser.newPage({viewport: {width: 800, height: 1080}});
    await page.goto('http://127.0.0.1:8123/index.php?char=chocola-lolita');
    await page.waitForTimeout(3000);
    
    // Screenshot 1: Closed UI with toggle icon visible
    await page.screenshot({path: '/tmp/web_ui_closed.png'});
    console.log('Saved /tmp/web_ui_closed.png');

    // Click toggle icon to open sidebar
    await page.click('#toggle-icon');
    await page.waitForTimeout(1000);
    await page.screenshot({path: '/tmp/web_ui_open.png'});
    console.log('Saved /tmp/web_ui_open.png');

    // Click on Chocola drawer to expand
    const chocolaItem = await page.$('.menu-item');
    if (chocolaItem) {
      await chocolaItem.click();
      await page.waitForTimeout(600);
      await page.screenshot({path: '/tmp/web_ui_chocola.png'});
      console.log('Saved /tmp/web_ui_chocola.png');
    }

    // Click on Settings drawer to expand
    const settingsItem = await page.$('.settings-item');
    if (settingsItem) {
      await settingsItem.click();
      await page.waitForTimeout(600);
      await page.screenshot({path: '/tmp/web_ui_settings.png'});
      console.log('Saved /tmp/web_ui_settings.png');
    }

    await browser.close();
  } finally {
    php.kill();
  }
})();
