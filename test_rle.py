from PIL import Image
import os

def rgb_to_565(r, g, b):
    # Swap R and B for CYD BGR display
    return ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3)

def swap_bytes(val16):
    return ((val16 & 0xFF) << 8) | ((val16 >> 8) & 0xFF)

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
        
        pixels = []
        for py in range(232):
            for px in range(320):
                r, g, b = frame.getpixel((px, py))
                rgb565 = rgb_to_565(r, g, b)
                rgb565 = (~rgb565) & 0xFFFF
                rgb565 = swap_bytes(rgb565)
                pixels.append(rgb565)
                
        # RLE compress
        compressed = []
        i = 0
        while i < len(pixels):
            run_len = 1
            while i + run_len < len(pixels) and pixels[i+run_len] == pixels[i] and run_len < 255:
                run_len += 1
            compressed.append((run_len, pixels[i]))
            i += run_len
            
        total_size += len(compressed) * 3 # 1 byte for length, 2 bytes for color
print(f"RLE compressed size: {total_size} bytes")
