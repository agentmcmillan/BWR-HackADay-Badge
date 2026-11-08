// SPDX-License-Identifier: GPL-3.0-or-later
/* Badge image conversion. OpenDisplay handles perceptual matching and diffusion.
 * Natural mode restricts red to saturated red source regions, preserving neutral photos.
 * Optional relative panel calibration comes from the three photographed reference charts.
 * Ink indices remain canonical; preview RGB is a photo-derived estimate. */
(function(root){
  'use strict';
  const clamp=(v,a=0,b=1)=>Math.max(a,Math.min(b,v));
  const PANEL_PROFILE={"id":"badge-bwr-photo-v2","colors":{"white":[255,255,255],"black":[63,63,63],"red":[180,52,39]},"mono_input_lut":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,17,18,20,21,23,25,27,29,31,33,35,37,39,41,43,46,48,50,52,55,57,60,62,64,67,69,72,74,76,79,81,83,85,88,90,92,94,95,97,99,101,102,104,105,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,127,128,128,129,129,130,130,131,131,132,132,133,133,134,134,135,135,136,136,137,137,138,138,139,139,140,141,141,142,142,143,143,144,145,145,146,146,147,147,148,149,149,150,150,151,152,152,153,153,154,155,155,156,156,157,158,158,159,160,160,161,162,163,164,164,165,166,167,168,168,169,170,171,172,172,173,174,175,176,177,177,178,179,180,181,182,182,183,184,185,186,187,188,189,189,190,191,192,193,194,194,195,196,197,197,198,199,200,201,201,202,203,204,205,206,206,207,208,209,210,211,211,212,213,214,215,216,217,217,218,219,220,221,222,223,223,224,225,226,226,227,228,229,229,230,231,232,232,233,234,235,235,236,237,238,238,239,240,241,241,242,243,244,245,245,246,247,248,249,249,250,251,252,253,253,254,255],"default_strength":0.5};
  const linearTable=Float32Array.from({length:256},(_,v)=>v/255<=.04045?(v/255)/12.92:((v/255+.055)/1.055)**2.4);
  function monoForPanel(input,exposure,strength){
    const result=new Uint8ClampedArray(input.length),gain=Math.pow(2,Number(exposure)||0);
    for(let i=0;i<input.length;i+=4){
      const y=clamp(gain*(.2126*linearTable[input[i]]+.7152*linearTable[input[i+1]]+.0722*linearTable[input[i+2]]));
      const source=Math.round(255*(y<=.0031308?12.92*y:1.055*y**(1/2.4)-.055));
      const value=Math.round(source+(PANEL_PROFILE.mono_input_lut[source]-source)*strength);result.set([value,value,value,255],i);
    }
    return result;
  }

  const smooth=(a,b,v)=>{const t=clamp((v-a)/(b-a));return t*t*(3-2*t);};
  function redConfidence(r,g,b){
    const hi=Math.max(r,g,b),lo=Math.min(r,g,b),delta=hi-lo;
    if(!delta||hi!==r)return 0;
    let hue=60*((g-b)/delta);hue=Math.abs(hue);
    return (1-smooth(10,24,hue))*smooth(.52,.78,delta/hi);
  }
  function convert(data,width,height,options={}){
    if(!Number.isInteger(width)||!Number.isInteger(height)||width<1||height<1||width*height>2000000||data.length!==width*height*4)throw Error('Invalid image dimensions.');
    const lib=root.OpenDisplay;if(!lib)throw Error('Image engine is unavailable. Reload the page.');
    const color=options.color!==false, natural=options.preset!=='color',calibrated=options.calibrated!==false;
    const modes={stucki:lib.DitherMode.STUCKI,burkes:lib.DitherMode.BURKES,atkinson:lib.DitherMode.ATKINSON,none:lib.DitherMode.NONE};
    const artwork=options.preset==='artwork';
    const settings={mode:options.dither===false?lib.DitherMode.NONE:(modes[options.algorithm]??(artwork?lib.DitherMode.NONE:lib.DitherMode.STUCKI)),
      serpentine:true,exposure:Math.pow(2,Number(options.exposure)||0),saturation:1,
      shadows:artwork?0:.10,highlights:artwork?0:.06,
      dbs:options.detail&&!artwork?{viewingDistanceCm:50,ppi:121,maxPasses:2}:false};
    const input=new Uint8ClampedArray(data);
    // Matte transparency onto white consistently in both conversion passes.
    for(let i=0;i<input.length;i+=4){const a=input[i+3]/255;for(let c=0;c<3;c++)input[i+c]=Math.round(input[i+c]*a+255*(1-a));input[i+3]=255;}
    const inks=calibrated?PANEL_PROFILE.colors:{white:[255,255,255],black:[0,0,0],red:[180,0,0]};
    const rgb=values=>({r:values[0],g:values[1],b:values[2]});
    const panelPalette={colors:{black:rgb(inks.black),white:rgb(inks.white),red:rgb(inks.red)},accent:'red',scheme:lib.ColorScheme.BWR};
    const supportsTone=calibrated&&!artwork&&(natural||!color)&&(options.algorithm||'stucki')==='stucki'&&options.dither!==false&&!options.detail;
    const strength=supportsTone?clamp(options.calibrationStrength===undefined?PANEL_PROFILE.default_strength:Number(options.calibrationStrength)):0;
    const monoSettings=strength>0?{...settings,exposure:1,shadows:settings.shadows*(1-strength),highlights:settings.highlights*(1-strength)}:settings;
    const monoInput=strength>0?monoForPanel(input,options.exposure,strength):input;
    const monoImage=()=>lib.ditherImage({width,height,data:monoInput},lib.ColorScheme.MONO,monoSettings);
    const result=color?lib.ditherImage({width,height,data:input},calibrated?panelPalette:lib.ColorScheme.BWR,
      calibrated&&!artwork?{...settings,tone:'auto',gamut:'auto'}:settings):monoImage();
    const indices=result.indices;
    if(color&&natural){
      // A separate mono pass prevents error diffusion from injecting red into grey/skin.
      const mono=monoImage().indices;
      const matrix=[0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5];
      for(let y=0;y<height;y++)for(let x=0;x<width;x++){
        const i=y*width+x,confidence=redConfidence(input[4*i],input[4*i+1],input[4*i+2]);
        if(confidence<=(matrix[(y&3)*4+(x&3)]+.5)/16)indices[i]=mono[i];
      }
    }
    // Preserve exact source ink colors, including white margins, after optional tone fitting.
    for(let i=0;i<indices.length;i++){
      const r=input[4*i],g=input[4*i+1],b=input[4*i+2];
      if(r===0&&g===0&&b===0)indices[i]=0;
      else if(r===255&&g===255&&b===255)indices[i]=1;
      else if(color&&r===255&&g===0&&b===0)indices[i]=2;
    }
    const packed=new Uint8Array(color?Math.ceil(width*height/5):Math.ceil(width*height/8));
    const rgba=new Uint8ClampedArray(width*height*4),counts=[0,0,0];
    for(let i=0;i<indices.length;i++){
      const ink=indices[i];if(ink> (color?2:1))throw Error('Unexpected palette index.');
      // Library: black=0 white=1 red=2. Badge: white=0 black=1 red=2.
      const trit=ink===0?1:ink===1?0:2;counts[trit]++;
      if(color)packed[(i/5)|0]+=trit*[1,3,9,27,81][i%5];else if(trit===1)packed[i>>3]|=128>>(i&7);
      const rgb=trit===1?inks.black:trit===2?inks.red:inks.white;rgba.set([...rgb,255],4*i);
    }
    return {packed,rgba,width,height,counts,previewPalette:[inks.white,inks.black,inks.red],profile:calibrated?PANEL_PROFILE.id:'generic'};
  }
  root.EinkCore={convert,redConfidence,profile:PANEL_PROFILE};
})(globalThis);
