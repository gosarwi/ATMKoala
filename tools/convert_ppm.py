import sys
from pathlib import Path
from PIL import Image

if len(sys.argv) != 3:
    raise SystemExit("usage: convert_ppm.py INPUT.ppm OUTPUT.png")
source, target = map(Path, sys.argv[1:])
Image.open(source).save(target, "PNG")
