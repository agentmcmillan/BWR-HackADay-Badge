// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
// USB transport for the badge settings portal.
//
// Replaces window.fetch for the portal's own endpoints (/cfg, /save, /image,
// /photos, /photo, /photos/play) and carries each request over Web Serial as one
// text line per frame:
//   request:  @BC1 <id> <verb> [<session>] [<args>]\n
//   response: @BC1 <id> <http-status> <base64 body>\n
// Other serial output (boot and game logs) is ignored. Uploads are sent as
// 768-byte chunks; a busy badge (503) is retried for up to 40 seconds.
//
// Expects the host portal page to define: #load, #code and #save elements, and
// the globals unlock (the #load click handler), imageAbort, imageEpoch,
// renderBusy, saving, img, gray, rgb and hadImg.
const CHUNK_BYTES = 768;
const RPC_TIMEOUT_MS = 8000;
const BUSY_RETRIES = 40;
class BadgeSerial {
  constructor() {
    this.port = null;
    this.writer = null;
    this.reader = null;
    this.pending = new Map();
    this.sequence = 0;
    this.session = '';
    this.uploading = false;
    this.onDisconnect = () => {};
  }
  async connect(port) {
    if (this.port) await this.close();
    await port.open({baudRate: 115200, bufferSize: 32768});
    this.port = port;
    this.writer = port.writable.getWriter();
    this.reader = port.readable.getReader();
    this.reading = this.readLoop();
    try {
      const hello = JSON.parse(await this.rpc('hello'));
      if (hello.protocol !== 1 || !/^[a-f0-9]{16}$/.test(hello.session)) throw Error('Unsupported badge protocol.');
      this.session = hello.session;
      return hello;
    } catch (error) {
      await this.close();
      throw error;
    }
  }
  async close() {
    const port = this.port;
    this.port = null;
    this.session = '';
    for (const p of this.pending.values()) p.reject(Error('Badge disconnected.'));
    this.pending.clear();
    if (this.reader) {
      await this.reader.cancel().catch(() => {});
      await this.reading;
      this.reader = null;
    }
    if (this.writer) { this.writer.releaseLock(); this.writer = null; }
    if (port) await port.close().catch(() => {});
  }
  async readLoop() {
    let buffer = '';
    const decoder = new TextDecoder();
    try {
      for (;;) {
        const {value, done} = await this.reader.read();
        if (done) break;
        buffer += decoder.decode(value, {stream: true});
        let newline;
        while ((newline = buffer.indexOf('\n')) >= 0) {
          const line = buffer.slice(0, newline).trim();
          buffer = buffer.slice(newline + 1);
          const frame = /^@BC1 (\d+) (\d+) ([A-Za-z0-9+/]*={0,2})$/.exec(line);
          if (!frame) continue; // normal boot and game logs are not protocol messages
          const p = this.pending.get(Number(frame[1]));
          if (!p) continue;
          this.pending.delete(Number(frame[1]));
          let body;
          try {
            body = new TextDecoder().decode(Uint8Array.from(atob(frame[3]), c => c.charCodeAt(0)));
          } catch (_) {
            // A corrupted frame fails only its own request, not the connection.
            p.reject(Error('Badge sent a corrupted response.'));
            continue;
          }
          if (frame[2] === '200') p.resolve(body);
          else { const error = Error(body); error.status = Number(frame[2]); p.reject(error); }
        }
        if (buffer.length > 32768) buffer = ''; // bounded even for arbitrary serial output
      }
    } catch (_) {
      // Physical disconnect is reported below, including pending requests.
    } finally {
      this.reader.releaseLock();
      for (const p of this.pending.values()) p.reject(Error('Badge disconnected.'));
      this.pending.clear();
      this.session = '';
      // close() clears this.port before cancelling the reader. If the port is still
      // set, the device went away: release the writer and close the port here too.
      const port = this.port;
      if (port) {
        this.port = null;
        this.reader = null;
        if (this.writer) {
          try { this.writer.releaseLock(); } catch (_) {}
          this.writer = null;
        }
        port.close().catch(() => {});
      }
      this.onDisconnect();
    }
  }
  rpc(verb, args = '') {
    if (!this.writer) return Promise.reject(Error('Connect the badge first.'));
    const id = ++this.sequence;
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending.delete(id);
        reject(Error('Badge did not respond. Use firmware 0.8.9 or newer in normal running mode.'));
      }, RPC_TIMEOUT_MS);
      const finish = fn => value => { clearTimeout(timeout); fn(value); };
      this.pending.set(id, {resolve: finish(resolve), reject: finish(reject)});
      const line = `@BC1 ${id} ${verb}${verb === 'hello' ? '' : ' ' + this.session}${args ? ' ' + args : ''}\n`;
      this.writer.write(new TextEncoder().encode(line)).catch(error => {
        this.pending.delete(id); clearTimeout(timeout); reject(error);
      });
    });
  }
  async retryBusy(verb, args = '') {
    for (let attempt = 0; ; ++attempt) {
      try { return await this.rpc(verb, args); }
      catch (error) {
        if (error.status !== 503 || attempt >= BUSY_RETRIES - 1) throw error;
        await new Promise(resolve => setTimeout(resolve, 1000));
      }
    }
  }
  async upload(kind, bytes, extra = '') {
    if (this.uploading) throw Error('Another upload is in progress. Wait for it to finish.');
    this.uploading = true;
    const session = this.session;
    try {
      await this.rpc('begin', `${kind} ${bytes.length}${extra ? ' ' + extra : ''}`);
      try {
        for (let offset = 0; offset < bytes.length; offset += CHUNK_BYTES) {
          if (this.session !== session) throw Error('Badge connection changed during upload.');
          const block = bytes.subarray(offset, offset + CHUNK_BYTES);
          const encoded = btoa(String.fromCharCode(...block));
          await this.rpc('chunk', `${offset} ${encoded}`);
        }
        if (this.session !== session) throw Error('Badge connection changed during upload.');
        return await this.retryBusy('commit');
      } catch (error) {
        // An old upload must never abort a new session's transaction.
        if (session && this.session === session) await this.rpc('abort').catch(() => {});
        throw error;
      }
    } finally {
      this.uploading = false;
    }
  }

}
const badgeSerial = new BadgeSerial();
const nativeFetch = window.fetch.bind(window);
window.fetch = async (url, options = {}) => {
  if (!['/cfg', '/save', '/image', '/photos', '/photo', '/photos/play'].includes(url)) return nativeFetch(url, options);
  if (!badgeSerial.session) return new Response('Connect the badge first.', {status: 503});
  try {
    if(url==='/photos')return new Response(await badgeSerial.retryBusy('photos'));
    if(url==='/photos/play')return new Response(await badgeSerial.rpc('photo_play'));
    if(url==='/photo'){
      const slot=options.headers['X-Photo-Slot'];
      const body=options.method==='DELETE'?await badgeSerial.retryBusy('photo_delete',slot):
        await badgeSerial.upload('photo',options.body,`${slot} ${btoa(options.headers['X-Photo-Name'])}`);
      return new Response(body);
    }
    const body = url === '/cfg' ? await badgeSerial.retryBusy('cfg') :
      await badgeSerial.upload(url === '/image' ? 'image' : 'settings',
        typeof options.body === 'string' ? new TextEncoder().encode(options.body) : options.body);
    return new Response(body, {status: 200});
  } catch (error) {
    // Badge-supplied codes outside the error range would make Response() throw.
    const status = error.status >= 400 && error.status <= 599 ? error.status : 503;
    return new Response(error.message, {status});
  }
};
window.addEventListener('DOMContentLoaded', async () => {
  const get = id => document.getElementById(id);
  const button = get('load');
  const unlock = button.onclick;
  get('code').hidden = true;
  button.closest('fieldset').querySelector('legend').textContent = 'USB connection';
  const help = button.closest('fieldset').querySelector('small');
  help.textContent = 'Connect the badge with a USB data cable in normal running mode, then choose its serial port.';
  button.textContent = 'Connect badge';
  const state = document.createElement('p');
  state.setAttribute('role', 'status');
  button.after(state);
  const disconnect = document.createElement('button');
  disconnect.textContent = 'Disconnect';
  disconnect.hidden = true;
  state.after(disconnect);
  let connecting = false;
  badgeSerial.onDisconnect = () => {
    if(window.photoDisconnected)window.photoDisconnected();
    get('save').disabled = true;
    disconnect.hidden = true;
    button.textContent = 'Connect badge';
    state.textContent = 'Disconnected. Reconnect after the badge restarts.';
  };
  disconnect.onclick = () => badgeSerial.close();
  async function connect(port) {
    if (connecting) return;
    connecting = true; button.disabled = true;
    state.textContent = 'Connecting…';
    let connected = false;
    try {
      const info = await badgeSerial.connect(port);
      connected = true;
      get('code').value = info.session;
      if(imageAbort)imageAbort.abort(); imageEpoch++; renderBusy=false;
      saving = false; img = gray = rgb = null; hadImg = false;
      state.textContent = `Badge ${info.badge} · firmware ${info.version} · USB connected`;
      button.textContent = 'Reload settings';
      disconnect.hidden = false;
      await unlock();
    } catch (error) {
      if (connected) {
        // The USB session is up; only loading settings failed. Keep it open.
        state.textContent = `Connected, but settings could not be loaded: ${error.message}`;
      } else {
        state.textContent = `${error.message} Close any serial monitor that is using the port.`;
        get('save').disabled = true;
      }
    } finally { connecting = false; button.disabled = false; }
  }
  button.onclick = async () => {
    if (badgeSerial.session) return unlock();
    try {
      const port = await navigator.serial.requestPort({filters: [{usbVendorId: 0x303a, usbProductId: 0x1001}]});
      await connect(port);
    } catch (error) {
      state.textContent = error.name === 'NotFoundError' ? 'No port selected.' : error.message;
    }
  };
  if (!('serial' in navigator)) {
    button.disabled = true;
    state.textContent = 'Open this page in desktop Chrome or Edge using HTTPS or localhost.';
    return;
  }
  // Only auto-open an unambiguous, previously granted ESP32-S3 port.
  const ports = (await navigator.serial.getPorts()).filter(port => {
    const info = port.getInfo();
    return info.usbVendorId === 0x303a && info.usbProductId === 0x1001;
  });
  if (ports.length === 1) await connect(ports[0]);
});

window.addEventListener('DOMContentLoaded',()=>{
  const area=document.createElement('fieldset');
  area.innerHTML='<legend>Original firmware</legend><p>Restore the original firmware backed up from this badge. This replaces the current OS, settings and photos. The Mac saves the current flash first.</p><p>On the badge, open <strong>Original</strong> and hold <strong>A + B for 3 seconds</strong>. Then use the button below.</p><button id="restore-original">Restore original firmware…</button><p id="restore-status" role="status"></p>';
  document.body.append(area);
  const button=document.getElementById('restore-original'),status=document.getElementById('restore-status');
  button.onclick=async()=>{
    if(!badgeSerial.session){status.textContent='Connect the badge first.';return;}
    if(!['127.0.0.1','localhost'].includes(location.hostname)){status.textContent='Use the local Mac page at http://127.0.0.1:8766/ for original firmware restoration.';return;}
    button.disabled=true;
    try{
      const ticket=await badgeSerial.rpc('restore_ticket');
      if(!window.confirm('Restore the original firmware now? This replaces the current OS, settings and photos. Keep the USB cable connected. A backup of the current flash will be saved on this Mac.')){button.disabled=false;return;}
      await badgeSerial.close();
      const response=await nativeFetch('/restore-original',{method:'POST',headers:{'Content-Type':'application/json','X-Badge-Action':'restore-original'},body:JSON.stringify({ticket})});
      if(!response.ok)throw Error('The local restore service could not start. Reconnect and unlock Original again.');
      // Poll the local restore service once a second until it finishes.
      for (;;) {
        const reply = await nativeFetch('/restore-status');
        if (!reply.ok) throw Error('Lost contact with the local restore service.');
        const progress = await reply.json();
        status.textContent = progress.message;
        if (progress.state !== 'running') {
          if (progress.state === 'error') button.disabled = false;
          break;
        }
        await new Promise(resolve => setTimeout(resolve, 1000));
      }
    }catch(error){status.textContent=error.message;button.disabled=false;}
  };
});
