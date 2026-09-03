# Cảm biến từ trường và gia tốc (Low-Power Instrument)

Dự án thiết kế phần cứng và phần mềm đo cảm biến từ trường & gia tốc công suất thấp sử dụng ESP32 và KiCad.

## 📁 Cấu trúc dự án

- **hardware/**: Chứa toàn bộ thiết kế phần cứng (KiCad, sơ đồ nguyên lý, thư viện linh kiện).
  - **schematics/**: Sơ đồ nguyên lý PDF (schematic_Qcomment_24_12_25.pdf).
  - **kicad/**: File thiết kế nguyên bản KiCad v6/v7/v8 (.kicad_sch, .kicad_pcb).
  - **Dependencies/**: Thư viện footprint & 3D model linh kiện.
- **irmware/**: Mã nguồn vi điều khiển ESP32 (esp32_wifi_telemetry).
- **docs/**: Tài liệu hướng dẫn & hình ảnh minh họa.

## 🛠️ Công nghệ & Linh kiện sử dụng

- **Vi điều khiển**: ESP32 / ESP32-S3
- **Cảm biến**: MMC5983MA (Từ trường), ASM330LHHTR (Gia tốc/Góc quay)
- **Phần mềm thiết kế**: KiCad v6+
