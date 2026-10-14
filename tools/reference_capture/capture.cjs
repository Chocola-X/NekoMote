const {chromium} = require('playwright');
const fs = require('node:fs');
(async () => {
    const options=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
    const browser=await chromium.launch({executablePath:options.browser,headless:true,
        args:['--enable-webgl','--ignore-gpu-blocklist','--use-angle=gl']});
    try {
        const page=await browser.newPage({viewport:{width:options.width,height:options.height},deviceScaleFactor:1});
        const errors=[];
        const messages=[];
        page.on('pageerror',error=>errors.push(String(error)));
        page.on('console',message=>{
            if(messages.length<100) messages.push(message.type()+': '+message.text());
            if(message.type()==='error') errors.push(message.text());
        });
        await page.goto(options.url,{waitUntil:'load',timeout:60000});
        await page.waitForFunction(()=>window.ready,{timeout:60000});
        let result;
        let captureTimer;
        try {
            result=await Promise.race([
                page.evaluate(options=>window.capture(options),options),
                new Promise((_,reject)=>{
                    captureTimer=setTimeout(()=>reject(new Error('reference capture timed out')),
                                            options.captureTimeoutMs||60000);
                })
            ]);
        }
        catch(error) {
            // Failed/empty output can still explain an invalid procedural input.
            // Preserve submitted GPU data only; it never becomes a passing reference.
            if(options.traceGeometry) {
                let failureTimer;
                try {
                    const observation=await Promise.race([
                        page.evaluate(()=>({trace:window.drawTrace||[],state:window.stateTrace||[]})),
                        new Promise((_,reject)=>{
                            failureTimer=setTimeout(()=>reject(new Error('failure observation timed out')),1000);
                        })
                    ]);
                    fs.writeFileSync(options.output+'.failure.trace.json',JSON.stringify(observation,null,2)+'\n');
                }
                catch(observationError) {messages.push(String(observationError));}
                finally {clearTimeout(failureTimer);}
            }
            fs.writeFileSync(options.output+'.failure.json',JSON.stringify({
                ...options,error:String(error),console:messages,pageErrors:errors},null,2)+'\n');
            throw new Error(String(error)+'\n'+messages.join('\n'));
        }
        finally {clearTimeout(captureTimer);}
        if (result.metadata.glError) throw new Error('WebGL error: '+result.metadata.glError);
        if (errors.length) throw new Error(errors.join('\n'));
        fs.writeFileSync(options.output+'.png',Buffer.from(result.png.split(',')[1],'base64'));
        fs.writeFileSync(options.output+'.json',JSON.stringify({...options,...result.metadata},null,2)+'\n');
        if (options.trace) {
            fs.writeFileSync(options.output+'.trace.json',JSON.stringify(result.trace));
            fs.writeFileSync(options.output+'.state.json',JSON.stringify(result.stateTrace));
            if(options.traceMaskPixels) fs.writeFileSync(options.output+'.mask-pixels.json',JSON.stringify(result.maskPixels));
        }
        console.log(JSON.stringify(result.metadata));
    } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
