# Changelog

This project follows [Semantic Versioning](https://semver.org/). Dates use the
ISO 8601 format.

## 1.0.0 - 2026-11-08

The first stable release of the independently licensed display components,
host tools, integration adapters, tests and research notes developed for the
Hackaday Supercon 2026 Billboard Badge.

### Highlights

- Added portable byte-raster, name-tag, Spirograph, DVD bounce and Game of Life
  rendering components with high-contrast e-paper styling.
- Added PackBits video storage, whole-frame and delta playback, adaptive band
  scheduling, temporal-stable conversion, poster/light-grid treatments and a
  centered quarter-area Smol renderer.
- Added authenticated USB control, bounded refresh benchmarks, automated host
  tooling and reproducible optical-analysis helpers.
- Documented the panel data path, a 22-index known command/register catalog,
  TCON experiments, complete readable OTP map, trigger/BUSY timing, factory
  fast-mode findings and a ranked speed roadmap.
- Added sanitizer-backed C tests plus Python and JavaScript coverage for the
  portable display, video, USB and browser components.

### Scope and safety

This repository contains original additions, documentation and anonymized
measurements. It does not redistribute the upstream badge OS, stock artwork,
private benchmark media or complete firmware images. The experimental panel
modes are outside the manufacturer's supported operating path and may cause
ghosting, accelerated wear or permanent panel damage.
