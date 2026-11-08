# Licensing and provenance

## Public scope

Original additions in this repository are licensed under **GPL-3.0-or-later**,
matching the license of ESPARGOS/esp-sdr used in the private firmware integration.
See [LICENSE](../LICENSE). This grant applies only to work we can license; it
is not a license for the omitted badge OS or panel driver.

| Included material | Terms |
| --- | --- |
| Project C modules, adapters, tools, tests and documentation | GPL-3.0-or-later |
| `vendor/font8x8_basic.h` | Upstream public-domain notice retained |
| `web/vendor/epaper-dithering.js` | OpenDisplay.org MIT; license text included |
| OpenDithering-derived processing | MIT notice in `web/vendor/THIRD_PARTY_NOTICES.md` |

## Sources checked and omitted

| Source | Revision checked | Status |
| --- | --- | --- |
| [supercon26-badge-os](https://github.com/yigitabi5444/supercon26-badge-os) | `52ced66d0360164c02f4542247e93e6296cf677a` | No project license grant found in checked tree |
| [supercon26-badge-idf](https://github.com/yigitabi5444/supercon26-badge-idf) | `45daa5783fcd5d74b6b56028cd597e77a1a81915` | No project license grant found in checked tree |
| [official Billboard Badge](https://github.com/Hack-a-Day/2026-Billboard_Badge) | `9c55a516632eddfc2fe774e393407c0765e8cb13` | No project license file found in checked tree |

The public tree excludes the inherited settings HTML page, those firmware implementations, their artwork, the
upstream game implementations, and linked firmware binaries. API names and
porting instructions identify integration points; they do not redistribute the
upstream implementation. A complete private cleanup/build was retained locally
pending license clarification.

The checked heads are provenance references, not a claim that the private
integration copied every file from those latest revisions. Its inherited bundle
was based on badge-os v0.8. The private SDR integration uses
[ESPARGOS/esp-sdr](https://github.com/ESPARGOS/esp-sdr), notably commit
`9b61e7465bee6da15209c5c09308043d08c46a1d`, under GPL-3.0-or-later. SDR implementation
files are not part of this limited public snapshot.

Attribution is not a substitute for a license grant. Do not add omitted upstream
code under this repository's license without permission from its rights holder.
