#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
genfontcn.py - 适用于 STM32 OLED 的轻量级汉字点阵提取脚本
"""

import os
import sys
import re
import glob
from pathlib import Path

# 尝试导入 Pillow 库
try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("\n[错误] 未检测到 Pillow 库！")
    print("请先在终端运行以下命令安装：")
    print("    pip install Pillow\n")
    sys.exit(1)

# CJK 汉字正则范围 (Unicode: 0x4E00 ~ 0x9FFF)
CJK_REGEX = re.compile(r'[\u4e00-\u9fff]')

def find_project_root():
    """定位工程根目录（寻找包含 Core 文件夹的路径）"""
    current = Path.cwd()
    if (current / "Core").is_dir():
        return current
    if (current.parent / "Core").is_dir():
        return current.parent
    if (current.parent.parent / "Core").is_dir():
        return current.parent.parent
    return current

def get_system_cjk_font():
    """智能查找跨平台的中文字体（优先适配 macOS 和 Windows）"""
    font_candidates = [
        # macOS 常见中文字体路径
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/Library/Fonts/Songti.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
        "/Library/Fonts/Arial Unicode.ttf",
        # Windows 常见中文字体路径
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simsun.ttc",
        # Linux
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    ]
    
    for path in font_candidates:
        if os.path.exists(path):
            try:
                font = ImageFont.truetype(path, 16)
                print(f"[信息] 成功加载系统字体: {path}")
                return font
            except Exception:
                continue

    for name in ["PingFang SC", "Heiti SC", "SimHei", "Microsoft YaHei"]:
        try:
            font = ImageFont.truetype(name, 16)
            print(f"[信息] 成功根据名称加载字体: {name}")
            return font
        except Exception:
            pass

    print("[警告] 未找到常见系统黑体，使用默认字体")
    return ImageFont.load_default()

def collect_chinese_chars(project_root):
    """扫描工程源码提取所有汉字"""
    chars = set()
    
    search_patterns = [
        str(project_root / "Core" / "Src" / "**" / "*.c"),
        str(project_root / "Core" / "Inc" / "**" / "*.h"),
    ]
    
    scanned_files = 0
    for pattern in search_patterns:
        for file_path in glob.glob(pattern, recursive=True):
            scanned_files += 1
            try:
                with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
                    for line in f:
                        for lit in re.findall(r'"([^"\n]*)"', line):
                            for ch in CJK_REGEX.findall(lit):
                                chars.add(ch)
            except Exception as e:
                print(f"[跳过] 读取文件 {file_path} 出错: {e}")

    # 收集命令行额外传入的汉字参数
    if len(sys.argv) > 1:
        for arg in sys.argv[1:]:
            for ch in CJK_REGEX.findall(arg):
                chars.add(ch)

    print(f"[信息] 扫描了 {scanned_files} 个源码文件，共提取到 {len(chars)} 个唯一汉字")
    return sorted(list(chars), key=lambda c: ord(c))

def render_glyph(ch, font):
    """
    将单个汉字渲染为 16x16 点阵，打包为 32 字节（列扫描，页格式）
    页 0: y=0..7, 16 列
    页 1: y=8..15, 16 列
    """
    img = Image.new("1", (16, 16), 0)
    draw = ImageDraw.Draw(img)

    code = ord(ch)
    try:
        draw.text((8, 8), ch, font=font, fill=1, anchor="mm")
    except TypeError:
        draw.text((0, 0), ch, font=font, fill=1)

    px = img.load()
    data = []

    for page in range(2):
        for x in range(16):
            b = 0
            for y in range(8):
                if px[x, page * 8 + y]:
                    b |= (1 << y)
            data.append(b)

    return code, data

def main():
    root = find_project_root()
    print(f"[信息] 工程根目录: {root}")

    font = get_system_cjk_font()
    char_list = collect_chinese_chars(root)
    if not char_list:
        print("[提示] 源码中未发现任何汉字。可以通过命令行补充，如: python3 genfontcn.py 光热监控舵机控制密码修改")
        return

    glyphs = []
    for ch in char_list:
        code, data = render_glyph(ch, font)
        glyphs.append((ch, code, data))

    # 写入 Core/Src/oled_font_cn.c
    c_file_path = root / "Core" / "Src" / "oled_font_cn.c"
    os.makedirs(c_file_path.parent, exist_ok=True)
    
    with open(c_file_path, "w", encoding="utf-8") as f:
        f.write("/* 由 genfontcn.py 自动生成，请勿手动编辑 */\n")
        f.write("#include \"oled_font.h\"\n\n")
        f.write("const OLED_CnGlyph_t OLED_FontCn[OLED_FONT_CN_COUNT] = {\n")
        
        for ch, code, data in glyphs:
            f.write(f"    /* 字: '{ch}', Unicode: 0x{code:04X} */\n")
            f.write(f"    {{ 0x{code:04X}, {{\n")
            line1 = ", ".join([f"0x{b:02X}" for b in data[:16]])
            line2 = ", ".join([f"0x{b:02X}" for b in data[16:]])
            f.write(f"        {line1},\n")
            f.write(f"        {line2}\n")
            f.write("    } },\n")
        
        f.write("};\n")
    print(f"[成功] 点阵数据已写入: {c_file_path}")

    # 更新或生成 Core/Inc/oled_font.h
    h_file_path = root / "Core" / "Inc" / "oled_font.h"
    count_val = len(glyphs)

    if h_file_path.exists():
        with open(h_file_path, "r", encoding="utf-8") as f:
            content = f.read()
        
        if re.search(r"#define\s+OLED_FONT_CN_COUNT\s+\d+U?", content):
            new_content = re.sub(
                r"#define\s+OLED_FONT_CN_COUNT\s+\d+U?",
                f"#define OLED_FONT_CN_COUNT {count_val}U",
                content
            )
            with open(h_file_path, "w", encoding="utf-8") as f:
                f.write(new_content)
            print(f"[成功] 已同步更新 {h_file_path.name} 中的字数宏: {count_val}U")
        else:
            print(f"[提示] 未在 {h_file_path.name} 中找到 OLED_FONT_CN_COUNT 宏定义，请手动确认")
    else:
        # 使用普通字符串模板替换，避免任何大括号解析歧义
        header_template = """#ifndef __OLED_FONT_H
#define __OLED_FONT_H

#include "main.h"

#define OLED_FONT_CN_COUNT __COUNT__U

typedef struct
{
    uint16_t code;      /* Unicode 码点 */
    uint8_t data[32];   /* 16x16 点阵数据 (2页 x 16列) */
} OLED_CnGlyph_t;

extern const OLED_CnGlyph_t OLED_FontCn[OLED_FONT_CN_COUNT];

const uint8_t *OLED_FontCnLookup(uint16_t code);

#endif /* __OLED_FONT_H */
"""
        with open(h_file_path, "w", encoding="utf-8") as f:
            f.write(header_template.replace("__COUNT__", str(count_val)))
        print(f"[成功] 已自动创建头文件: {h_file_path}")

if __name__ == "__main__":
    main()