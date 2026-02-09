#Build
$project = "D:\MESHCORE\FIRMWARE\meshcore-firmware"
$envs = "Heltec_v3_companion_radio_ble","Heltec_v3_repeater","heltec_v4_companion_radio_ble","heltec_v4_repeater","Heltec_t114_companion_radio_ble" #"Heltec_v3_companion_radio_wifi","heltec_v4_companion_radio_wifi"

foreach ($e in $envs) {
    Write-Host "=== Building $e ==="
    C:\Users\MILESHKIN\.platformio\penv\Scripts\platformio.exe run -d $project -e $e
}
#Clean
$project = "D:\MESHCORE\FIRMWARE\meshcore-firmware"
$envs = "Heltec_v3_companion_radio_ble","Heltec_v3_repeater","heltec_v4_companion_radio_ble","heltec_v4_repeater"

foreach ($e in $envs) {
    Write-Host "=== Cleaning $e ==="
    C:\Users\MILESHKIN\.platformio\penv\Scripts\platformio.exe run -d $project --target clean --environment $e
}

#Convert HEX to UF2
py bin\uf2conv\uf2conv.py .pio\build\Heltec_t114_companion_radio_ble\firmware.hex -c -o .pio\build\Heltec_t114_companion_radio_ble\firmware.uf2 -f 0xADA52840

py bin\uf2conv\uf2conv.py .pio\build\Heltec_t114_without_display_companion_radio_ble\firmware.hex -c -o .pio\build\Heltec_t114_without_display_companion_radio_ble\firmware.uf2 -f 0xADA52840

py bin\uf2conv\uf2conv.py .pio\build\Heltec_t114_repeater\firmware.hex -c -o .pio\build\Heltec_t114_repeater\firmware.uf2 -f 0xADA52840
