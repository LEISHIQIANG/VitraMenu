import os
from PIL import Image

target_blue = (60, 140, 230) # Same soft blue
target_sizes = [(16, 16), (32, 32), (64, 64)]

def process_and_maximize_iconcache():
    ico_dir = r"e:\LEIFOLDER\LEI-plug\VitraMenu\ico"
    png_name = "图标缓存.png"
    ico_name = "iconcache.ico"
    png_path = os.path.join(ico_dir, png_name)
    
    if not os.path.exists(png_path):
        print(f"File not found: {png_path}")
        return

    try:
        img = Image.open(png_path).convert("RGBA")
        
        # 1. Tight Crop (Remove all padding since user forgot to manual scale)
        bbox = img.getbbox()
        if bbox:
            img = img.crop(bbox)
            
        # 2. Stretch to Square (Tangency logic)
        # We'll use a 256 master to keep it sharp
        master = img.resize((256, 256), Image.Resampling.LANCZOS)
        
        # 3. Colorize with soft blue
        alpha = master.split()[3]
        blue_img = Image.new("RGBA", master.size, target_blue + (255,))
        final_img = Image.new("RGBA", master.size, (0,0,0,0))
        final_img.paste(blue_img, (0,0), mask=alpha)
        master = final_img
        
        # 4. Save as ICO with all standard frames
        frames = []
        for size in target_sizes:
            resized = master.resize(size, Image.Resampling.LANCZOS)
            frames.append(resized)
            
        ico_path = os.path.join(ico_dir, ico_name)
        frames[0].save(ico_path, format="ICO", sizes=target_sizes, append_images=frames[1:])
        print(f"Maximized and Colorized: {ico_name}")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    process_and_maximize_iconcache()
