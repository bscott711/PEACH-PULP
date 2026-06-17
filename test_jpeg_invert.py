from PIL import Image, ImageOps

img = Image.new("RGB", (100, 100), "red")
r, g, b = img.split()
img = Image.merge("RGB", (b, g, r)) # swap R and B
img = ImageOps.invert(img) # invert
img.save("test_inv.jpg", "JPEG")
