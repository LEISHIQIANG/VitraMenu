
import os
from PIL import Image

def process_icon(input_path, output_path):
    try:
        img = Image.open(input_path).convert("RGBA")
        target_sizes = [(16, 16), (32, 32)]
        frames = []
        for size in target_sizes:
            resized = img.resize(size, Image.Resampling.LANCZOS)
            frames.append(resized)
        
        # Save as ICO with only 16x16 and 32x32
        frames[0].save(output_path, format="ICO", sizes=target_sizes, append_images=frames[1:])
        print(f"Successfully processed icon: {output_path}")
    except Exception as e:
        print(f"Error processing icon: {e}")

if __name__ == "__main__":
    input_ico = r"g:\LEI-PLUG\VitraMenu\ico\codex.ico"
    # We will overwrite it or save to a temp and then move? 
    # Let's save to a temporary name first just in case.
    output_ico = r"g:\LEI-PLUG\VitraMenu\ico\codex_processed.ico"
    process_icon(input_ico, output_ico)
