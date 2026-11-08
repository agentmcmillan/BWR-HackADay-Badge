const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const root=path.resolve(__dirname,'../web');
const context={atob:global.atob,TextDecoder,TextEncoder,console};vm.createContext(context);
for(const name of ['vendor/epaper-dithering.js','eink-core.js'])vm.runInContext(fs.readFileSync(path.join(root,name),'utf8'),context);
const convert=(data,w,h,options={})=>context.EinkCore.convert(data,w,h,options);
function solid(r,g,b,w=64,h=64){const d=new Uint8ClampedArray(w*h*4);for(let i=0;i<d.length;i+=4)d.set([r,g,b,255],i);return d;}
for(const [rgb,value] of [[[0,0,0],121],[[255,255,255],0],[[255,0,0],242]]){
 const out=convert(solid(...rgb),64,64);
 assert(out.packed.subarray(0,Math.floor(4096/5)).every(v=>v===value),`${rgb} changed solid ink`);
}
for(const value of [32,96,128,192,240]){
 const out=convert(solid(value,value,value),64,64);
 assert.equal(out.counts[2],0,'Neutral gray acquired red');
 if(value>32&&value<240)assert(out.counts[0]>0&&out.counts[1]>0,'Midtone collapsed to one ink');
}
assert.equal(context.EinkCore.redConfidence(190,130,110),0,'Skin-tone guard');
assert.equal(convert(solid(190,130,110),64,64).counts[2],0);
const gradient=new Uint8ClampedArray(256*64*4);
for(let y=0;y<64;y++)for(let x=0;x<256;x++)gradient.set([x,0,0,255],4*(y*256+x));
const red=convert(gradient,256,64);
assert(red.counts[1]>500&&red.counts[2]>500,'Dark red shading collapsed');
const gray=new Uint8ClampedArray(256*64*4);
for(let y=0;y<64;y++)for(let x=0;x<256;x++)gray.set([x,x,x,255],4*(y*256+x));
const ramp=convert(gray,256,64,{color:false});
let previous=-1;
for(let band=0;band<16;band++){
 let white=0;for(let y=0;y<64;y++)for(let x=band*16;x<(band+1)*16;x++)white+=ramp.rgba[4*(y*256+x)]===255;
 const mean=white/(16*64);assert(mean>=previous-.03,'Tone ramp reversed');previous=mean;
}
const transparent=solid(0,0,0);for(let i=3;i<transparent.length;i+=4)transparent[i]=0;
assert(convert(transparent,64,64).packed.every(v=>v===0),'Transparent input did not matte to white');
for(const color of [true,false]){
 const out=convert(gradient,256,64,{color});
 for(let i=0;i<256*64;i++){
  const t=color?Math.floor(out.packed[(i/5)|0]/[1,3,9,27,81][i%5])%3:((out.packed[i>>3]>>(7-(i&7)))&1);
  const expected=Array.from(out.previewPalette[t]);
  assert.deepEqual(Array.from(out.rgba.subarray(i*4,i*4+3)),expected);
 }
}
const profile=context.EinkCore.profile;
assert.equal(profile.mono_input_lut.length,256);assert.equal(profile.mono_input_lut[0],0);assert.equal(profile.mono_input_lut[255],255);
for(let i=1;i<256;i++)assert(profile.mono_input_lut[i]>=profile.mono_input_lut[i-1]);
const calibrated=convert(solid(128,128,128),64,64,{color:false});
const generic=convert(solid(128,128,128),64,64,{color:false,calibrated:false});
const none=convert(solid(128,128,128),64,64,{color:false,calibrationStrength:0});
const full=convert(solid(128,128,128),64,64,{color:false,calibrationStrength:1});
assert(full.counts[1]<calibrated.counts[1]&&calibrated.counts[1]<none.counts[1],'Half strength must sit between both endpoints');
assert.equal(profile.default_strength,.5);
for(const algorithm of ['burkes','atkinson']){
 const a=convert(solid(128,128,128),64,64,{color:false,algorithm,calibrationStrength:0});
 const b=convert(solid(128,128,128),64,64,{color:false,algorithm,calibrationStrength:1});
 assert.deepEqual(Array.from(a.packed),Array.from(b.packed),'Do not apply the Stucki curve to another kernel');
}
assert(calibrated.counts[1]<generic.counts[1],'Calibration should reduce excessive midtone black coverage');
assert.deepEqual(Array.from(generic.previewPalette[1]),[0,0,0]);
assert.throws(()=>convert(new Uint8ClampedArray(4),960,624));
console.log('PASS rendering: solid inks, neutral/skin red protection, dark-red shading, monotonic gray ramp, white alpha matte, and exact preview-to-packed-wire mapping.');
