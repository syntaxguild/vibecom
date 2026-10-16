# Development and simulation

รันคำสั่งทั้งหมดจากราก repository

## Build และจำลอง

ติดตั้ง PlatformIO IDE และ Wokwi Simulator ใน VS Code แล้วรัน:

```sh
pio run -e esp32dev -e esp32vibecom13 -e esp32vibecom_sonar -e esp32vibecom -e esp32sim
```

| Environment | จอ | ใช้กับ |
| --- | --- | --- |
| `esp32vibecom` | Waveshare 17344 จอ IPS 2 นิ้ว แนวนอน + SG90 Digital | เครื่องจริง โมเดล v0.6 / VL53L1X |
| `esp32vibecom_sonar` | Waveshare 17344 จอ 2 นิ้ว + SG90 Digital | เครื่องจริง โมเดล v0.2–v0.3 / HC-SR04 เดิม |
| `esp32vibecom13` | Waveshare 15867 จอ 1.3 นิ้ว + SG90 Digital | เครื่องจริง โมเดล v0.1 เดิม |
| `esp32dev` | ST7789 240×240 แบบ 7 ขา + เซอร์โวเดิม | เครื่องจริง แบบเดิม |
| `esp32sim` | ILI9341 320×240 แนวนอน | Wokwi เท่านั้น |

โปรไฟล์เริ่มต้นของ PlatformIO คือ `esp32vibecom` สำหรับรุ่นอ้างอิง อัปโหลดด้วย `pio run -e esp32vibecom -t upload` ถ้าใช้จอ 7 ขาเดิมให้ระบุ `-e esp32dev` โดยชัดเจน เปิด Serial ที่ 115200 baud ด้วย `pio device monitor`

สำหรับ Wokwi ให้ build `pio run -e esp32sim` แล้วกด F1 → **Wokwi: Start Simulator** ไฟล์ `wokwi.toml` ชี้ไปที่ `.pio/build/esp32sim/firmware.bin` และ `firmware.elf` พร้อม Custom Chip VL53L1X ที่คอมไพล์ไว้ ถ้าเปิดผัง HC-SR04 จากรุ่นก่อนค้างอยู่ ให้หยุด Simulator แล้ว **Developer: Reload Window** เพื่อโหลดผัง/Custom Chip ใหม่ก่อนเริ่ม ต้อง build และเริ่ม Simulator ใหม่หลังแก้โค้ด

หลังเริ่มต้องเห็น `Display=READY` และค่า `FPS draw` / `TFT` ขยับจากศูนย์หลังผ่านช่วงเริ่มต้น หากเคยใช้รุ่นที่ขึ้น `RGB565 canvas allocation failed` ให้ build โปรไฟล์ `esp32sim` ใหม่ก่อนเริ่มจำลอง รุ่นนี้ใช้บัฟเฟอร์ที่จองไว้ตั้งแต่ build เพื่อแก้จอดำจากการจองหน่วยความจำก้อนใหญ่ก่อน `setup()`

Wokwi ใช้ **ILI9341 เป็นตัวแทนแสดงภาพสีด้วยไดรเวอร์ของมันเอง** ไม่ใช่โมเดล ST7789 และไม่ใช้ยืนยันการเริ่มทำงานหรือความเร็วของจอจริง ไม่ต้องซื้อ ILI9341 จอจำลองหมุนเป็นแนวนอน 320×240 ทั้งตัวจอและภาพ ใบหน้าตรรกะ 240×160 ขยายเป็น 320×214 อยู่กึ่งกลางที่ (0, 13) ส่วนจอจริง 2 นิ้ว ST7789 วางแนวนอน 320×240 และใช้ใบหน้าขยายเป็น 320×214 ที่ (0, 13); โปรไฟล์ 1.3 นิ้วเดิมยังเป็น 240×240 วางหน้าที่ (0, 40)

คลิกชิป **VL53L1X ToF (I2C model)** เพื่อเปลี่ยน `Distance (cm)` เริ่มที่ 100 cm หรือปรับ `Target detected` เป็น 0 เพื่อทดสอบไม่มีเป้าหมาย ตัวจำลองใช้ I²C และไดรเวอร์ Pololu ตัวเดียวกับโปรไฟล์จริง แต่ไม่ได้จำลองแสง/FoV/crosstalk ครบ อ่าน [ขอบเขตและวิธีใช้ Custom Chip](../sim/README.md) สวิตช์ **TTP223 OUT (simulation)** เลื่อนขวา = แตะ HIGH เลื่อนซ้าย = ปล่อย LOW เป็นการจำลองสัญญาณ OUT ไม่ใช่ความจุไฟฟ้าของเซ็นเซอร์จริง

## ตรวจสอบ

```sh
pio run -e esp32dev -e esp32vibecom13 -e esp32vibecom_sonar -e esp32vibecom -e esp32sim
python tests/run_render_checks.py
```

Host test ใช้ **behavior, ท่าทาง, renderer และ Adafruit GFX ตัวเดียวกับเฟิร์มแวร์** ตรวจการกรองระยะ/ค่าหลุด/ขอบเขตอารมณ์, ช่วงทักทายซ้ำ, แตะหนึ่งครั้ง/สองครั้ง/ค้าง/สัญญาณเด้ง, หลับ/ปลุก, ตัวจับเวลาวนกลับ, คำสั่งผิดและขอบเขตค่าตั้ง, การ encode/decode ค่าที่บันทึก และการจำกัดมุมคอ

ส่วนภาพตรวจ 605 เฟรม Angry ระหว่างเปลี่ยนอารมณ์/กะพริบ และ 976 เฟรมของท่าทางร่วมกับอารมณ์ ตรวจตายังมองเห็น ไม่มีรอยยิ้มจากอารมณ์เก่าแทรก และ Happy/Love ต่างกันทั้งรูปทรง เพิ่มการตรวจ pixel ทุกตำแหน่งหลังขยาย 4:3 ความต่อเนื่องของทุกแถบ และ guard ปลายบัฟเฟอร์ รวมถึงบัฟเฟอร์อยู่ภายใน object, การเปลี่ยน palette, การขยายสี RGB565, ขอบเขตแถบภาพ และไม่มีปากทุกโหมด ไม่ใช้เฟรมเวิร์กทดสอบ

GitHub Actions ใน `.github/workflows/checks.yml` build ทั้งห้าโปรไฟล์ รัน host checks ทั้ง legacy/reference/large/ToF รวมการแปลงหน่วยและการปฏิเสธ status/bus/range ที่ผิด ตลอดจน stale timeout/reacquisition และตรวจไฟล์ STL/GLB ที่ส่งออกจริง เมื่อ push / pull request หรือสั่งเอง ล็อก platform `espressif32 @ 7.1.3` ตามเวอร์ชันที่ตรวจบิลด์แล้ว ตรวจผล run ล่าสุดได้ในแท็บ Actions ของ repo; checker ไฟล์โมเดลใช้ Python มาตรฐานและไม่ต้องติดตั้ง Blender บน CI

ต้องมี g++ C++11 ขึ้นไป หรือระบุ `--compiler` สำหรับ Windows ติดตั้งได้ด้วย `pio pkg install --global --tool platformio/toolchain-gccmingw32` ไฟล์ `tests/host` แทนเฉพาะชนิดข้อมูล Arduino ที่ GFX ต้องการ ไม่ได้จำลองบัสหรือฮาร์ดแวร์

สำหรับ ToF มี harness แยกใน `tests/chip-host` ที่เชื่อม C callbacks ของ Custom Chip กับไลบรารี Pololu 1.3.1 จริง ตรวจ init/timing/readiness, หน่วย slider, status ไม่มีเป้าหมาย และ Companion Happy/Idle/Angry รวมถึงตรวจว่า `diagram.json` ต่อ SDA21/SCL22 และ source/JSON/WASM ตรงกัน การทดสอบนี้ไม่ใช่การรัน ESP32 บน Wokwi backend

สร้างภาพ/GIF ใหม่ด้วย `python tests/run_render_checks.py --preview` ต้องมี Pillow เพิ่มเติม

ตรวจใน Wokwi / เครื่องจริงต่อ:

1. ระยะ 100 cm ไม่แตะ: Idle สีฟ้า ไม่มีรูม่านตาหรือจุดกลมในตา
2. 30 cm → 10 cm: Happy → Angry ต้องมีตาโกรธตั้งแต่เฟรมแรกที่วาดสำหรับ Angry ไม่มีปากทั้งสองโหมด
3. แตะค้างอย่างน้อย 500 ms ที่ 10 cm: Love สีชมพูชนะระยะ ตายังทรงเดิมและกะพริบได้
4. ปล่อย: Love ต่อ 1.2 วินาที แล้วกลับ Angry ไม่มีรอยยิ้ม/แก้มค้าง ทำซ้ำและดูช่วงกะพริบ
5. 100 cm คงที่ 10 วินาที: Sleepy แล้วแตะปลุก
6. ทดสอบแตะสั้นหนึ่งครั้งและสองครั้งที่ 100 cm ดู `Action=attention` / `Action=play`; เดินจาก 100 → 30 cm ดู `Action=greeting`
7. ตั้งค่า → `save` → รีสตาร์ต ตรวจ `Settings: loaded.` และ `show` ว่าค่าตรง; ตรวจมุมคอทุกท่าไม่เกินช่วงที่ตั้ง
8. ตรวจ `Display=READY` แล้วค่า FPS ขยับจากศูนย์ เครื่องจริงให้ตรวจสี การต่อขา ค่า TFT / SPI max ทุกโหมด และเซอร์โวยังตอบสนองระหว่างส่งจอ
9. เครื่องจริง ToF: ตรวจ `VL53L1X READY`, วัดวัตถุ 10/30/100 cm หลายสีและหลายสภาพแสง ตรวจสัญญาณหายว่าไม่ค้าง Happy/Angry และยังแตะหัวได้; จำลองเซ็นเซอร์ไม่พร้อมโดยปิดไฟก่อนถอดสาย แล้วเปิดเครื่องเพื่อดู ERROR ทดสอบนี้ต้องใช้บอร์ดจริง Wokwi ไม่ได้ยืนยันสภาพบัส/optics ของฮาร์ดแวร์

Wokwi รุ่น VL53L1X ปัจจุบันได้รับการยืนยันจากผู้ใช้ว่าเปิดผังใหม่และรันใน VS Code ได้แล้ว เป็นผลที่ผู้ใช้รายงาน ไม่ใช่การวัดฮาร์ดแวร์จริง การทดสอบจอ ST7789, ToF จริง, การเก็บค่าข้ามรีสตาร์ต และ FPS ยังต้องทำบนบอร์ดจริง

## ไฟล์และการย้อนกลับ

- `src/main.cpp`: อินพุต อารมณ์ เซอร์โว SPI และสถิติ
- `src/hardware.h`: โปรไฟล์จอ/เซอร์โว ขีดจำกัดคอและ namespace ค่าตั้ง
- `src/distance_reading.h`: ตรวจ status/บัส/ขอบเขตค่า ToF และแปลง mm เป็น cm ก่อนกรอง
- `mechanical/`: โมเดล 3D รุ่นอ้างอิง source และ checker
- `src/behavior.h`: กรองระยะ อารมณ์ gesture ท่าทาง ค่าตั้งและการตรวจข้อมูล ใช้ร่วมกับ host test
- `src/face.h`: renderer RGB565 ที่ใช้ร่วมกับ host test
- `platformio.ini`: ไลบรารีล็อกเวอร์ชันและโปรไฟล์จริง/จำลอง
- `diagram.json`, `wokwi.toml`: วงจรตัวแทนและเฟิร์มแวร์จำลอง
- `sim/`: Custom Chip VL53L1X I²C, WASM พร้อมใช้, source และข้อจำกัดการจำลอง
- `tests/run_render_checks.py`, `tests/behavior_checks.cpp`, `tests/face_render.cpp`: ตรวจพฤติกรรม/ภาพและสร้าง preview
- `.github/workflows/checks.yml`: build และ host checks อัตโนมัติ

OLED SSD1306 เดิมอยู่ใน Git commit `dff0ec6` หากย้อนกลับให้ใช้เฟิร์มแวร์ วงจร และไลบรารีรุ่นนั้นร่วมกัน ไม่เสียบ OLED กับเฟิร์มแวร์ ST7789

อ้างอิง: [ST7789 / hardware SPI](https://learn.adafruit.com/adafruit-1-3-and-1-54-240-x-240-wide-angle-tft-lcd-displays/arduino-wiring-test), [ILI9341 ใน Wokwi](https://docs.wokwi.com/parts/wokwi-ili9341), [ตั้งค่า Wokwi](https://docs.wokwi.com/vscode/project-config), [สวิตช์จำลอง](https://docs.wokwi.com/parts/wokwi-slide-switch), [HC-SR04](https://docs.wokwi.com/parts/wokwi-hc-sr04), [TTP223](https://www.tontek.com.tw/product/product2?idx=243&lang=en)

การบันทึกค่าอ้างอิง [Preferences ของ Espressif](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html); CI อ้างอิง [PlatformIO GitHub Actions](https://docs.platformio.org/en/latest/integration/ci/github-actions.html)


## CAD checks

Blender 5.2 is required to regenerate CAD. CI checks the shipped artifacts and their fingerprints without running Blender.

```sh
blender --background --factory-startup --python-exit-code 1 --python mechanical/build_model.py
blender --background --python-exit-code 1 --python mechanical/audit_assembly.py -- --strict
blender --background --python-exit-code 1 --python mechanical/check_robustness.py -- --strict
python mechanical/check_exports.py
python scripts/check_docs.py
```

Use `-- --check-only` with `build_model.py` for geometry checks without rewriting exports. Full CAD instructions: [mechanical](../mechanical/README.md).

## Release packages

```sh
python scripts/package_release.py
```

Packages are written to ignored `.pio/release/`. The version comes from `VERSION`; the CAD revision must match it. Source, STL, BLEND and GLB remain editable/reproducible.
