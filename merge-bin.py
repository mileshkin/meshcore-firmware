#!/usr/bin/python3

import os

Import("env", "projenv")

board_config = env.BoardConfig()
merged_bin = os.path.join("$BUILD_DIR", "${PROGNAME}-merged.bin")

def merge_bin_action(source, target, env):
    # Получаем путь к esptool
    esptool_path = env.get("OBJCOPY", "esptool.py")
    
    # Собираем список образов для прошивки
    flash_images = env.Flatten(env.get("FLASH_EXTRA_IMAGES", []))
    
    # Добавляем основной firmware
    app_offset = env.get("ESP32_APP_OFFSET", "0x10000")
    flash_images.extend([app_offset, target[0].get_abspath()])
    
    # Команда для объединения
    merge_cmd = [
        "$PYTHONEXE",
        esptool_path,
        "--chip", board_config.get("build.mcu", "esp32"),
        "merge_bin",
        "-o", env.subst(merged_bin),
        "--flash_mode", board_config.get("build.flash_mode", "dio"),
        "--flash_freq", env.subst("${__get_board_f_flash(__env__)}"),
        "--flash_size", board_config.get("upload.flash_size", "4MB"),
    ]
    
    # Добавляем образы
    for item in flash_images:
        merge_cmd.append(env.subst(str(item)))
    
    print("Merging firmware images...")
    result = env.Execute(" ".join(['"%s"' % arg if ' ' in str(arg) else str(arg) for arg in merge_cmd]))
    
    if result == 0:
        print(f"Merged binary created: {env.subst(merged_bin)}")
    
    return result

# Добавляем действие после сборки firmware
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin_action)