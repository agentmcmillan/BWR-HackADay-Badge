// SPDX-License-Identifier: GPL-3.0-or-later
importScripts('vendor/epaper-dithering.js','eink-core.js');
onmessage=event=>{
  try{
    const {data,width,height,options}=event.data,start=performance.now();
    const result=EinkCore.convert(data,width,height,options);result.ms=performance.now()-start;
    postMessage(result,[result.packed.buffer,result.rgba.buffer]);
  }catch(error){postMessage({error:error.message});}
};
