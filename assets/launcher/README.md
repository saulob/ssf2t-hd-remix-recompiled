# Launcher header art

`header-art.svg` is the source of truth. Edit it first; the palette lives in the `<style>` block at the
top in a few CSS classes (`.deep` `#210705`, `.red` `#7A1B10`, `.orange` `#D8551A`, `.amber` `#F2A541`,
`.highlight` `#FFE6A0`). Change a class there to restyle the art.

`header-art.png` is the runtime asset, generated from the SVG at 3840x432 (1600x180 viewBox at 2.4x)
with a transparent background. Do not edit it by hand; regenerate it after every SVG change.

From the repository root on Windows, with Edge installed:

```powershell
$svg = (Resolve-Path assets/launcher/header-art.svg).Path
$png = Join-Path (Resolve-Path assets/launcher).Path 'header-art.png'
& 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe' `
  --headless=new --disable-gpu --hide-scrollbars --default-background-color=00000000 `
  --window-size=3840,432 "--screenshot=$png" "file:///$($svg -replace '\\','/')"
```

The launcher draws the PNG over its header gradient, anchored to the header's top-right corner at the
full header height; the SVG's empty left side is what keeps the title readable.
