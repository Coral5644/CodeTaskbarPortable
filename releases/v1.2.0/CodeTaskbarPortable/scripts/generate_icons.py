"""Render the project's original geometric SVG design as PNG and Windows ICO.

Development only: requires Pillow. The application does not depend on Python.
Geometry below mirrors assets/icon.svg; keep both in sync when changing the icon.
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SCALE = 4
canvas = Image.new("RGBA", (256 * SCALE, 256 * SCALE))
mask = Image.new("L", canvas.size)
ImageDraw.Draw(mask).rounded_rectangle((8*SCALE, 8*SCALE, 248*SCALE, 248*SCALE), 52*SCALE, fill=255)
pixels = Image.new("RGBA", canvas.size)
draw = ImageDraw.Draw(pixels)
top, bottom = (23, 42, 70), (8, 15, 32)
for y in range(canvas.height):
    t = y / (canvas.height - 1)
    color = tuple(round(a + (b-a)*t) for a,b in zip(top,bottom)) + (255,)
    draw.line((0,y,canvas.width,y),fill=color)
canvas.paste(pixels,(0,0),mask)
draw = ImageDraw.Draw(canvas)
def box(coords, radius, fill, outline=None, width=1):
    draw.rounded_rectangle(tuple(round(v*SCALE) for v in coords), round(radius*SCALE), fill=fill, outline=outline, width=width*SCALE)
box((8,8,248,248),52,None,"#314967",3)
for y,color in ((51,"#16B6E9"),(139,"#8B6CF0")):
    box((34,y,222,y+66),16,color)
    box((53,y+21,78,y+46),6,"white")
    box((94,y+21,199,y+31),5,"white")
    # Alpha compositing keeps the SVG's secondary line opacity.
    layer = Image.new("RGBA",canvas.size)
    ld = ImageDraw.Draw(layer)
    ld.rounded_rectangle(tuple(round(v*SCALE) for v in (94,y+40,163,y+47)),round(3.5*SCALE),fill=(255,255,255,166))
    canvas = Image.alpha_composite(canvas,layer)
    draw = ImageDraw.Draw(canvas)
png = canvas.resize((256,256),Image.Resampling.LANCZOS)
png.save(ROOT/"assets"/"icon.png")
png.save(ROOT/"assets"/"app.ico", sizes=[(n,n) for n in (16,20,24,32,40,48,64,128,256)])
print("Generated assets/icon.png and assets/app.ico")
