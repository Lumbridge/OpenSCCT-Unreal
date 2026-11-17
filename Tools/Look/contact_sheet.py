"""Tiles screenshots into one image for a quick look: python contact_sheet.py <out.png> <shot>..."""
import sys

from PIL import Image

out, shots = sys.argv[1], sys.argv[2:]
columns = 2 if len(shots) > 1 else 1
width = 960
tiles = []
for path in shots:
    image = Image.open(path).convert("RGB")
    tiles.append(image.resize((width, int(image.height * width / image.width))))
rows = (len(tiles) + columns - 1) // columns
height = tiles[0].height
sheet = Image.new("RGB", (columns * width, rows * height))
for index, tile in enumerate(tiles):
    sheet.paste(tile, ((index % columns) * width, (index // columns) * height))
sheet.save(out)
