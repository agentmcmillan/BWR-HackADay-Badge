// SPDX-License-Identifier: GPL-3.0-or-later
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../web/serial.js'), 'utf8');
const context = vm.createContext({Uint8Array, btoa: global.btoa});
vm.runInContext(source.slice(0, source.indexOf('const badgeSerial =')) + '\nthis.BadgeSerial = BadgeSerial;', context);
(async () => {
  const client = new context.BadgeSerial();
  client.session = 'first';
  let release;
  const begin = new Promise(resolve => { release = resolve; });
  const calls = [];
  client.rpc = async verb => { calls.push(verb); if (verb === 'begin') await begin; return 'ok'; };
  client.retryBusy = client.rpc;
  const first = client.upload('photo', new Uint8Array([1, 2]));
  await assert.rejects(client.upload('settings', new Uint8Array([3])), /upload is in progress/);
  assert.deepEqual(calls, ['begin']);
  release();
  await first;
  assert.deepEqual(calls, ['begin', 'chunk', 'commit']);
  assert.equal(client.uploading, false);

  calls.length = 0;
  client.rpc = async verb => { calls.push(verb); if (verb === 'chunk') { client.session = 'replacement'; throw Error('disconnected'); } };
  await assert.rejects(client.upload('photo', new Uint8Array([1])), /disconnected/);
  assert.deepEqual(calls, ['begin', 'chunk']); // no abort sent to replacement
  assert.equal(client.uploading, false);
  console.log('PASS serial uploads: concurrent upload exclusion and no stale-session abort');
})().catch(error => { console.error(error); process.exitCode = 1; });
