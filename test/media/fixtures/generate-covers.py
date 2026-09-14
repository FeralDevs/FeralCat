"""Deterministic, synthetic decoder fixtures; needs Pillow. No user artwork."""
from pathlib import Path
from PIL import Image, ImageDraw
root = Path(__file__).parent
image = Image.new("RGB", (96, 96))
draw = ImageDraw.Draw(image)
for box, color in [((0, 0, 47, 47), "red"), ((48, 0, 95, 47), (0, 255, 0)),
                   ((0, 48, 47, 95), "blue"), ((48, 48, 95, 95), "white")]:
    draw.rectangle(box, fill=color)
image.save(root / "cover-quadrants.jpg", quality=95, subsampling=0)
image.save(root / "cover-progressive.jpg", quality=95, progressive=True)
Image.new("RGB", (400, 200), (220, 100, 20)).save(root / "cover-wide.jpg", quality=90)
Image.new("L", (32, 64), 180).save(root / "cover-grey.jpg", quality=90)
Image.new("RGB", (1, 1), (255, 0, 0)).save(root / "cover-tiny.jpg", quality=90)
Image.new("RGB", (401, 237), (30, 100, 240)).save(root / "cover-odd.jpg", quality=90)
Image.new("RGB", (1024, 1024), (200, 180, 50)).save(root / "cover-maximum.jpg", quality=90)
