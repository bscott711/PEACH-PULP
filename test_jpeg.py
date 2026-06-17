from PIL import Image
import os

img = Image.open('src/OTA_SpriteV2.png').convert('RGB')
w, h = img.size
cols, rows = 4, 3
border = 4
sw = (w - (cols-1)*border) // cols
sh = (h - (rows-1)*border) // rows

total_size = 0
for row in range(rows):
    for col in range(cols):
        x = col * (sw + border)
        y = row * (sh + border)
        frame = img.crop((x, y, x + sw, y + sh))
        frame = frame.resize((320, 232), Image.LANCZOS)
        # Apply color inversion if needed? Actually with JPEG we decode to RGB565 then push.
        # But TJpg_Decoder output can be customized.
        out = f"frame_{row}_{col}.jpg"
        frame.save(out, "JPEG", quality=85)
        total_size += os.path.getsize(out)

print(f"Total JPEG size: {total_size} bytes")
