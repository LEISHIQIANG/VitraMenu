import os
from PIL import Image

target_blue = (0, 120, 215) # Standard Microsoft Blue

mapping = {
    "DNS.png": "dns.ico",
    "HASH值.png": "hash.ico",
    "claudecode-color.png": "claudecode.ico",
    "host文件.png": "hosts.ico",
    "开始菜单.png": "startmenu.ico",
    "快捷重命名.png": "rename.ico",
    "提取文件.png": "extract.ico",
    "文件夹.png": "newfolder.ico", 
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

def solid_color_apply():
    ico_dir = r"e:\LEIFOLDER\LEI-plug\VitraMenu\ico"
    for png_name, ico_name in mapping.items():
        png_path = os.path.join(ico_dir, png_name)
        if not os.path.exists(png_path):
            continue
            
        try:
            img = Image.open(png_path).convert("RGBA")
            
            if png_name != "文件夹.png":
                # Get the alpha channel (transparency)
                alpha = img.split()[3]
                
                # Create a solid blue image of the same size
                blue_img = Image.new("RGBA", img.size, target_blue + (255,))
                
                # Mask the blue image with the original alpha channel
                # This makes the black parts (or any color parts) into the target blue
                # while keeping the exact shape and transparency.
                final_img = Image.new("RGBA", img.size, (0,0,0,0))
                final_img.paste(blue_img, (0,0), mask=alpha)
                img = final_img

            # Generate ICO
            frames = []
            for size in target_sizes:
                resized = img.resize(size, Image.Resampling.LANCZOS)
                frames.append(resized)
            
            ico_path = os.path.join(ico_dir, ico_name)
            frames[0].save(ico_path, format="ICO", sizes=target_sizes, append_images=frames[1:])
            print(f"Solid Blue Applied: {png_name}")
            
        except Exception as e:
            print(f"Error {png_name}: {e}")

if __name__ == "__main__":
    solid_color_apply()
