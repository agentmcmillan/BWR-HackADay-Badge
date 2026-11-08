# Browser image tools

Serve this directory with `python3 -m http.server 8766 --bind 127.0.0.1 --directory web`
from the repository root, then open http://127.0.0.1:8766/. The standalone preview
processes images locally in a worker. No upload or badge connection is required.

`eink-core.js` performs conversion; `eink-renderer.js` handles browser image loading,
resampling, cancellation and preview. `panel-profile.json` contains a relative
camera-derived ink/tone estimate from one badge. Its default tone correction is
50%; lighting, glare and panel variation limit its accuracy. It does not alter
physical refresh timing. Vendor revisions and MIT notices are in `vendor/`.

`serial.js` and `photo-ui.html` are integration fragments for a compatible settings
portal implementing the project's `@BC1` USB protocol. They are not loaded by the
standalone preview. They require host page controls and firmware endpoints that
are not included here. The inherited settings page is omitted because its
upstream project license was unresolved. The optional local restoration UI also
requires a private host service and verified device backup, neither distributed
here. Do not deploy it as a working restoration tool without those dependencies.

Run `node tests/test_eink_rendering.js` from the repository root to check ink
classification, tones, alpha compositing and preview-to-wire consistency.
