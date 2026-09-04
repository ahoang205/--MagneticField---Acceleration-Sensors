# HƯỚNG DẪN KẾT NỐI PHẦN CỨNG & HIỆU CHUẨN ĐIỂM KHÔNG (CALIBRATION)
**Dự án: Telemetry Từ Trường & IMU (STM32 & ESP32)**

Tài liệu này lưu trữ toàn bộ sơ đồ đấu nối dây, nguyên lý phần mềm và cẩm nang khắc phục sự cố được thống nhất trong quá trình phát triển dự án.

---

## 1. Sơ đồ đấu nối dây hệ thống (Wiring Connection)

Dưới đây là sơ đồ đấu nối hoàn chỉnh giữa mạch điều khiển STM32, ESP32, Module Relay 12V và nguồn tải LED 12V:

```text
[ Nguồn Adapter 12V ]
  (+) 12V -----------------------+-----------------------------+
  (-) GND -------+               |                             |
                 |               |                             |
                 |               |                             |
[ Module Relay 12V ]             |                             |
  VCC <----------|---------------+                             |
  GND <----------+                                             |
  IN  <-----------------------+                                |
  COM <-----------------------|--------------------------------+
  NO  ------------------------|------------------+
                              |                  |
[ Đèn LED 12V ]               |                  |
  (+) <-----------------------|------------------+
  (-) <----------+            |
                 |            |
[ Bo mạch STM32 ]|            |
  [ Cổng J4 - UART ]          |
    Chân 1 (GND) +------------+---------> Chân [ GND ] của ESP32
    Chân 3 (TX)  -----------------------> Chân [ RX2 (GPIO16) ] của ESP32
    Chân 4 (RX)  <----------------------- Chân [ TX2 (GPIO17) ] của ESP32
  [ Cổng J1 - LOAD 1 ] (Nếu kích LED bằng cổng J1)
    Chân 2 (Tải) -------------+
  [ Cổng J3 - LOAD 2 ] (Nếu kích LED bằng cổng J3)
    Chân 1 (Tải) -------------+
  [ Tụ đỏ C23 (+) ] <-------------------- Chân [ 5V / VIN ] của ESP32 (Chỉ nối khi không cắm Pin J2)
```

### Bảng chi tiết kết nối tín hiệu điều khiển:

| Thiết bị nguồn | Chân nguồn | Thiết bị đích | Chân đích | Chức năng / Lưu ý |
| :--- | :--- | :--- | :--- | :--- |
| **STM32 (J4)** | Chân 1 (GND) | **ESP32** | GND | Nối chung GND tham chiếu (Bắt buộc) |
| **STM32 (J4)** | Chân 3 (TX) | **ESP32** | RX2 (GPIO16) | Truyền gói telemetry cảm biến |
| **STM32 (J4)** | Chân 4 (RX) | **ESP32** | TX2 (GPIO17) | Nhận lệnh kích/ngắt tải LED |
| **ESP32** | 5V / VIN | **STM32** | Tụ C23 (+) | Cấp nguồn VCAP giả lập (nếu chạy debug USB) |
| **STM32 (J1)** | Chân 2 (Tải) | **Relay 12V** | IN | Tín hiệu kích mức thấp (Low-level trigger) |
| **Adapter 12V**| Cực Dương (+) | **Relay 12V** | VCC | Cấp nguồn nuôi cuộn dây relay |
| **Adapter 12V**| Cực Âm (-) | **Relay 12V** | GND | Nối chung GND hệ thống |

### Bảng chi tiết mạch lực đóng cắt tải LED:

| Thiết bị nguồn | Đường dây | Thiết bị đích | Đầu nối | Chức năng |
| :--- | :--- | :--- | :--- | :--- |
| **Adapter 12V**| Cực Dương (+) | **Relay 12V** | COM | Đường nguồn chờ |
| **Relay 12V** | Tiếp điểm NO | **Đèn LED 12V**| Dây Đỏ (+) | Cấp điện 12V khi đóng tiếp điểm |
| **Đèn LED 12V**| Dây Đen (-) | **Adapter 12V**| Cực Âm (-) | Khép kín vòng mạch tải |

---

## 2. Logic phần mềm & Nguyên lý hiệu chuẩn (Calibration)

### A. Logic kích hoạt LED/Relay:
Hệ thống sẽ bật sáng đèn LED khi thỏa mãn đồng thời 2 điều kiện:
1. Chênh lệch từ trường Z thực tế vượt ngưỡng cài đặt: $|\Delta Z - \text{Sai lệch tĩnh}| > \text{Ngưỡng cài đặt (mG)}$.
2. Mạch cảm biến phải hoàn toàn đứng yên (không bị rung lắc/chuyển động): `is_moving == false`.

### B. Hiệu chuẩn điểm không (Zero Baseline Calibration):
Do mỗi cảm biến từ trường MMC5983MA có độ lệch mặc định chế tạo khác nhau và ảnh hưởng từ trường nền xung quanh, chênh lệch tĩnh ban đầu ($\Delta Z_0$) có thể rất lớn (khoảng vài nghìn LSB ~ vài trăm mG).
* **Tự động hiệu chuẩn khi khởi động**: Trong 20 chu kỳ đo đầu tiên sau khi bật nguồn, ESP32 sẽ tự động khóa kích hoạt Relay và lấy trung bình chênh lệch từ trường làm điểm không tĩnh (`delta_z_zero`).
* **Hiệu chuẩn thủ công qua Web**: Giao diện Web GUI tích hợp nút **"HIỆU CHUẨN ĐIỂM KHÔNG"**. Khi nhấn nút, trình duyệt gửi lệnh WebSocket `CALIBRATE` tới ESP32 để gán lại điểm không tức thời bằng giá trị chênh lệch hiện tại.

---

## 3. Cẩm nang xử lý sự cố (Troubleshooting)

### Lỗi 1: Đèn LED/Relay bật sáng liên tục ngay khi cắm nguồn
* **Nguyên nhân**: Chân RX của STM32 (`PC5` - chân 4 cổng J4) bị hở mạch, lỏng dây hoặc chập GND. Code test phần cứng trên STM32 sẽ tự động bật tải LED/Relay khi chân này về mức thấp (0V).
* **Cách khắc phục**: Kiểm tra lại dây truyền tín hiệu từ **TX2 (GPIO17) của ESP32** về **chân 4 của J4 (STM32 RX)**. Hãy đảm bảo tiếp xúc tốt hoặc thay dây bus mới.

### Lỗi 2: Web không cập nhật chỉ số của cảm biến (Số liệu bằng 0)
* **Nguyên nhân**: ESP32 không nhận được bất kỳ tín hiệu UART nào từ STM32 gửi sang.
* **Cách khắc phục**:
  1. Kiểm tra xem chân **GND** giữa 2 mạch đã nối chung chưa.
  2. Tráo đổi dây nối giữa **Chân 3 cổng J4** (TX STM32) và **Chân 4 cổng J4** (RX STM32).
  3. Kiểm tra xem dây cấp nguồn giả lập VCAP (từ ESP32 VIN sang cực dương tụ đỏ C23 của STM32) có bị lỏng không.

### Lỗi 3: ESP32 bị khởi động lại liên tục hoặc treo tại màn hình bootloader
* **Nguyên nhân**: Do cắm nhầm dây UART vào các chân cấu hình khởi động của ESP32 (như GPIO12, GPIO2, GPIO0) hoặc do sụt áp nguồn cổng USB khi ESP32 bật WiFi phát AP.
* **Cách khắc phục**: 
  1. Rút toàn bộ dây kết nối tín hiệu ra, chỉ cắm cáp USB và kiểm tra xem ESP32 có khởi chạy WiFi bình thường trên Serial Monitor (115200 baud) hay không.
  2. Đổi cổng cắm USB trên máy tính (sử dụng cổng sau của PC) hoặc đổi sang một cáp USB tốt hơn để tránh sụt áp.
