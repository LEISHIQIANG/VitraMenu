import os
from PIL import Image, ImageOps

target_blue = (0, 120, 215) # Standard Microsoft Blue

mapping = {
    "DNS.png": "dns.ico",
    "HASH值.png": "hash.ico",
    "claudecode-color.png": "claudecode.ico",
    "host文件.png": "hosts.ico",
    "开始菜单.png": "startmenu.ico",
    "快捷重命名.png": "rename.ico",
    "提取文件.png": "extract.ico",
    "文件夹.png": "newfolder.ico", # Will be skipped for colorizing
    "文件结构.png": "struct.ico",
    "文件路径.png": "filepath.ico",
    "注册表.png": "regedit.ico",
    "清除只读.png": "readonly.ico",
    "用户所有权.png": "ownership.ico",
    "磁盘清理.png": "cleanup.ico",
    "编码转换.png": "encoding.ico",
    "解压文件内容.png": "unpack.ico",
    "解锁文件占用.png": "unlock.ico",
    "重启资源管理器.png": "restart.ico",
    "防火墙.png": "firewall.ico"
}

target_sizes = [(16, 16), (32, 32), (64, 64)]

def colorize_and_convert():
    ico_dir = r"e:\LEIFOLDER\LEI-plug\VitraMenu\ico"
    for png_name, ico_name in mapping.items():
        png_path = os.path.join(ico_dir, png_name)
        if not os.path.exists(png_path):
            continue
            
        try:
            img = Image.open(png_path).convert("RGBA")
            
            # Colorize if not the folder icon
            if png_name != "文件夹.png":
                # Extract alpha channel
                r, g, b, a = img.split()
                # Create a grayscale version to use as a mask/intensity guide
                gray = img.convert("L")
                
                # Apply the blue tint: 
                # We blend the blue color with the grayscale intensity to preserve highlights/shadows
                blue_fill = Image.new("RGB", img.size, target_blue)
                # Use ImageOps.colorize or just composite:
                # We want: output_rgb = target_blue * gray/255
                colorized_rgb = ImageOps.colorize(gray, (0, 0, 0), target_blue)
                img = Image.merge("RGBA", (colorized_rgb.split()[0], colorized_rgb.split()[1], colorized_rgb.split()[2], a))

            # Generate ICO
            frames = []
            for size in target_sizes:
                resized = img.resize(size, Image.Resampling.LANCZOS)
                frames.append(resized)
            
            ico_path = os.path.join(ico_dir, ico_name)
            frames[0].save(ico_path, format="ICO", sizes=target_sizes, append_images=frames[1:])
            print(f"Colorized & Converted: {png_name}")
            
        except Exception as e:
            print(f"Error {png_name}: {e}")

if __name__ == "__main__":
    colorize_and_convert()
