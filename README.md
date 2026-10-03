# BETA FilmLab OFX v1.0

True spatial film emulation for DaVinci Resolve (OFX plugin).

## What it does (that the DCTL cannot)

| Feature | How |
|---|---|
| **Optical halation** | Bright-pass → half-res separable Gaussian blur → amber-weighted screen composite. Real blur, not a tint. |
| **Gate weave** | Per-frame geometric displacement with bilinear resample |
| **Dust & scratches** | Temporally-coherent specks + vertical scratch lines |
| **Animated grain** | Procedural, tone-responsive, resolution-independent |

Plus the full photochemical pipeline: 13 camera logs → 5 film stocks (5219/5207/5203/5213/5222) → 2383 print → printer lights → vignette → flicker.

## Building

### macOS (for Raphael's Mac) — automatic via GitHub Actions
1. Push this folder to a GitHub repo
2. Actions → Build OFX → download `BETA_FilmLab.ofx-macOS`
3. No Xcode needed, no signing setup — GitHub builds it

### macOS — manual (Xcode command line tools)
```bash
curl -sL https://github.com/ofxa/openfx/archive/refs/heads/main.tar.gz -o openfx.tar.gz
mkdir -p deps && tar xzf openfx.tar.gz -C deps
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release
# bundle: build/BETA_FilmLab.ofx
```

### Linux
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

## Installing in Resolve

Copy the `.ofx` bundle to:
- macOS: `/Library/OFX/Plugins/`
- Linux: `/usr/OFX/Plugins/`
- Windows: `C:\Program Files\Common Files\OFX\Plugins\`

Restart Resolve. The plugin appears in Effects → OpenFX → **BETA IMAGE LAB → BETA FilmLab**.

## Controls (22)

- **Camera** (13): Sony S-Log3/S-Log2, Panasonic V-Log, Canon C-Log2/C-Log3, Nikon N-Log, DJI D-Log/D-Log2*, BMD Film Gen5, RED Log3G10, ARRI LogC3/LogC4, Apple Log
- **Film Stock** (5): 5219 500T, 5207 250D, 5203 50D, 5213 200T, 5222 Double-X BW
- **Exposure**, **Print Contrast**, **Printer R/G/B**
- **Grain**: Amount, Size, Gauge (Super 8/16/35/65), Shadows/Mids/Highlights, Colour, Seed
- **Halation**, **Halation Radius** (true optical blur)
- **Gate Weave**, **Dust & Scratches**, **Vignette**, **Film Flicker**

## Honest scope

- Camera decodes verified vs colour-science (<1e-9); D-Log2* is APPROX (DJI unpublished)
- Film params modeled from published Kodak H&D shapes, not digitized measurements
- Grain is procedural (no scanned plates in OFX without texture sampling)
- **Not yet tested in Resolve** — compiled and smoke-tested (dlopen) on Linux only. If Resolve refuses to load it, send the Console log.

## Credits

- OFX API: The OpenFX Association (ofxa/openfx)
- Grain approach: adapted from RCS Film Grain by Red Coral Studios (MIT)
