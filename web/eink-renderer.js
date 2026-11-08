// SPDX-License-Identifier: GPL-3.0-or-later
(function(root){
  'use strict';
  async function load(file){
    const url=URL.createObjectURL(file),image=new Image();
    try{image.src=url;await image.decode();return sample(image);}finally{URL.revokeObjectURL(url);}
  }
  function sample(image){
    const width=960,height=624,scale=Math.min(width/image.width,height/image.height);
    const dw=Math.max(1,Math.round(image.width*scale)),dh=Math.max(1,Math.round(image.height*scale));
    let source=image,sw=image.width,sh=image.height;
    // Progressive reduction averages fine source detail before final pixel quantization.
    while(sw>dw*2||sh>dh*2){
      const c=document.createElement('canvas');c.width=Math.max(dw,Math.round(sw/2));c.height=Math.max(dh,Math.round(sh/2));
      const ctx=c.getContext('2d');ctx.imageSmoothingEnabled=true;ctx.imageSmoothingQuality='high';ctx.drawImage(source,0,0,c.width,c.height);
      source=c;sw=c.width;sh=c.height;
    }
    const canvas=document.createElement('canvas');canvas.width=width;canvas.height=height;
    const ctx=canvas.getContext('2d');ctx.imageSmoothingEnabled=true;ctx.imageSmoothingQuality='high';
    ctx.fillStyle='white';ctx.fillRect(0,0,width,height);ctx.drawImage(source,Math.floor((width-dw)/2),Math.floor((height-dh)/2),dw,dh);
    return ctx.getImageData(0,0,width,height).data;
  }
  function render(data,options={},signal){
    if(typeof Worker==='undefined')return Promise.resolve(root.EinkCore.convert(data,960,624,options));
    return new Promise((resolve,reject)=>{
      if(signal?.aborted){reject(Error('Cancelled'));return;}
      const worker=new Worker(new URL('eink-worker.js',document.baseURI));let done=false;
      const finish=(error,result)=>{if(done)return;done=true;worker.terminate();signal?.removeEventListener('abort',abort);error?reject(error):resolve(result);};
      const abort=()=>finish(Error('Cancelled'));
      signal?.addEventListener('abort',abort,{once:true});
      worker.onmessage=event=>event.data.error?finish(Error(event.data.error)):finish(null,event.data);
      worker.onerror=()=>finish(Error('Could not start the image renderer. Reload the page.'));
      const copy=new Uint8ClampedArray(data);worker.postMessage({data:copy,width:960,height:624,options},[copy.buffer]);
    });
  }
  function preview(canvas,result){const ctx=canvas.getContext('2d'),image=ctx.createImageData(result.width,result.height);image.data.set(result.rgba);ctx.putImageData(image,0,0);}
  root.EinkRenderer={load,sample,render,preview};
})(globalThis);
