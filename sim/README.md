# VL53L1X ใน Wokwi

`diagram.json` ใช้ `chip-vl53l1x` ต่อ VIN → 3V3, GND → GND, SDA → GPIO21, SCL → GPIO22 และ `esp32sim` ใช้ Pololu VL53L1X 1.3.1 เหมือนโปรไฟล์เครื่องจริง ไม่มี HC-SR04 เป็นตัวแทนแล้ว

1. Build `pio run -e esp32sim`
2. ถ้าเปิด Wokwi ค้างจากรุ่นก่อน ให้หยุดแล้วใช้ **Developer: Reload Window** ใน VS Code เพื่อโหลด `wokwi.toml` และ Custom Chip ใหม่
3. เริ่ม **Wokwi: Start Simulator** และตรวจ `Distance sensor=VL53L1X READY`
4. คลิกชิป **VL53L1X ToF (I2C model)** แล้วปรับ `Distance (cm)` หรือ `Target detected` ค่า 0 คือไม่มีเป้าหมาย

ตัวชิปแสดงด้วยสัญลักษณ์ Custom Chip ของ Wokwi ไม่ใช่ภาพ PCB ของผู้ผลิต จอ ILI9341 ยังคงเป็นตัวแสดงภาพสีแทน ST7789 ตามที่ระบุบนผัง

## สิ่งที่จำลอง

- I²C 0x29 พร้อม register pointer 16 บิต, model ID, boot และการตั้ง timing ที่ไดรเวอร์ที่ pin ไว้ใช้
- Continuous data-ready ตาม inter-measurement period, interrupt-clear และค่าระยะ/status สำหรับ valid/missing target
- VIN/GND สำหรับ ACK/NACK ขั้นพื้นฐาน

โมเดลนี้ใช้ทดสอบการต่อสายและพฤติกรรมกับ **ไดรเวอร์ Pololu ที่ pin ไว้** ไม่ได้จำลองชิปทุก register, แสง, FoV, reflectance, crosstalk, calibration จริง, power consumption, address reassignment หรือ XSHUT และไม่ได้ยืนยันว่าอะไหล่จริงวัดระยะได้เหมือน slider

## สร้าง WASM ใหม่

มี WASM ที่คอมไพล์แล้วใน repo ใช้จำลองได้โดยไม่ต้องติดตั้ง compiler ถ้าแก้ source ให้ติดตั้ง [Wokwi CLI](https://github.com/wokwi/wokwi-cli) แล้วรัน:

```sh
python sim/build_chip.py --cli /path/to/wokwi-cli
python tests/run_render_checks.py
```

สคริปต์ build ใช้ `wokwi-cli chip compile` ซึ่งดาวน์โหลด WASI-SDK/`wokwi-api.h` ตาม [เอกสาร Wokwi](https://docs.wokwi.com/guides/custom-chips-to-wasm) และสร้าง fingerprint ของ C/JSON/WASM ให้ตรวจใน CI

Host checks เรียก C callback ของชิปผ่านไดรเวอร์ Pololu จริงและ Companion จริง ตรวจ init, หน่วยระยะ, data-ready/clear, ไม่มีเป้าหมาย, Happy/Idle/Angry และ NACK เมื่อไม่มีไฟ การทดสอบนี้ไม่ได้รัน ESP32 ภายใน Wokwi backend; การรัน CLI backend ต้องมี `WOKWI_CLI_TOKEN` ส่วน VS Code ใช้สิทธิ์ของ extension
