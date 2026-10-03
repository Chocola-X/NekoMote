const {chromium} = require('playwright');
const fs = require('node:fs');
(async () => {
    const options=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
    const browser=await chromium.launch({executablePath:options.browser,headless:true,
        args:['--enable-webgl','--ignore-gpu-blocklist','--use-angle=gl']});
    try {
        const page=await browser.newPage({viewport:{width:options.width,height:options.height},deviceScaleFactor:1});
        const errors=[];
        page.on('pageerror',error=>errors.push(String(error)));
        page.on('console',message=>{if(message.type()==='error') errors.push(message.text());});
        await page.goto(options.url,{waitUntil:'load',timeout:60000});
        await page.waitForFunction(()=>window.ready,{timeout:60000});
        const result=await page.evaluate(options=>window.capture(options),options);
        if (result.metadata.glError) throw new Error('WebGL error: '+result.metadata.glError);
        if (errors.length) throw new Error(errors.join('\n'));
        fs.writeFileSync(options.output+'.png',Buffer.from(result.png.split(',')[1],'base64'));
        fs.writeFileSync(options.output+'.json',JSON.stringify({...options,...result.metadata},null,2)+'\n');
        if (options.trace) fs.writeFileSync(options.output+'.trace.json',JSON.stringify(result.trace));
        console.log(JSON.stringify(result.metadata));
    } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
