#!/usr/bin/env python3
"""Regenerate native raw two-frame face tiles after icon or cutoff changes.
Run from the ROM root after `make tools`. Preserves palette indices verbatim.
"""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
names = 'pikachu bulbasaur charmander squirtle eevee jigglypuff meowth gengar treecko torchic mudkip ralts lotad spheal sableye absol'.split()
cutoffs = [22,22,21,21,21,26,20,24,20,20,22,20,26,27,21,20]
for gba in [False, True]:
 for female in [False, True]:
  output = bytearray()
  with tempfile.TemporaryDirectory(prefix='soul-face-') as temp:
   for name, cutoff in zip(names, cutoffs):
    variant = 'icon_gba.png' if gba else 'icon.png'
    if female and name == 'pikachu': variant = 'iconf.png'
    source = root / 'graphics/pokemon' / name / variant
    if not source.exists(): source = source.with_name('icon.png')
    target = Path(temp) / 'icon.4bpp'
    subprocess.run([str(root / 'tools/gbagfx/gbagfx'), str(source), str(target)], check=True)
    raw = bytearray(target.read_bytes()[:1024])
    assert len(raw) == 1024
    for frame in range(2):
     for y in range(cutoff + 1, 32):
      for tile_x in range(4):
       offset = (frame * 16 + (y // 8) * 4 + tile_x) * 32 + (y % 8) * 4
       raw[offset:offset+4] = bytes(4)
    output.extend(raw)
  suffix = ('_gba' if gba else '') + ('_female' if female else '')
  (root / f'graphics/soullink/faces{suffix}.4bpp').write_bytes(output)
