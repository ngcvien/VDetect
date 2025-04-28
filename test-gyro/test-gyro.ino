#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ... (Các #define cũ) ...

// --- Cấu hình MPU6050 & Nút nhấn ---
#define LED_PIN            19     // Chân LED (thay bằng LED_BUILTIN nếu dùng LED tích hợp)
#define BUZZER_PIN         18
#define BUTTON_PIN 0 // Chân nối nút nhấn
const unsigned long SET_REF_HOLD_TIME = 3000; // Thời gian nhấn giữ để đặt trục gốc (ms)
const float TILT_THRESHOLD_ACCEL = 5; // Ngưỡng gia tốc để xác định nghiêng (m/s^2) - Tinh chỉnh sau
                                         // ~arcsin(3.5/9.8) = 21 độ
const float UPSIDE_DOWN_THRESHOLD_Z = 0; // Ngưỡng trục Z để xác định lật ngược (khi Az đổi dấu và vượt qua 0)

// --- Biến toàn cục MPU6050 & Nút nhấn ---
Adafruit_MPU6050 mpu;
float refAx = 0.0, refAy = 0.0, refAz = 0.0; // Lưu giá trị gia tốc tham chiếu
bool referenceSet = false;                   // Cờ báo đã đặt tham chiếu hay chưa
unsigned long buttonPressStartTime = 0;      // Thời điểm bắt đầu nhấn nút
bool settingReference = false;               // Cờ báo đang trong quá trình nhấn giữ

// ... (Các biến toàn cục khác: Danh bạ, GPS, isRinging, buffers) ...

// --- Khai báo hàm ---
// ... (Các hàm cũ) ...
void handleSetReferenceButton(); // Hàm xử lý nút nhấn đặt tham chiếu
void readAndProcessMPU();      // Hàm đọc MPU và xác định độ nghiêng
void beep_led(int count, int duration_ms = 100);

void setup() {
  Serial.begin(115200);
  // ... (Khởi tạo LED, Buzzer, Enable SIM, EEPROM, Serial SIM, Serial GPS như cũ) ...
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  // --- Khởi tạo I2C và MPU6050 ---
  Serial.println("Khoi tao I2C & MPU6050...");
  if (!Wire.begin()) { // Khởi tạo I2C (có thể chỉ định chân SDA, SCL nếu cần: Wire.begin(21, 22);)
       Serial.println("!!! LOI KHOI TAO I2C !!!");
       while(1) delay(10);
  }
  if (!mpu.begin()) {
    Serial.println("!!! Khong tim thay chip MPU6050 !!!");
    while (1) {
      delay(10);
    }
  }
  Serial.println("MPU6050 da tim thay!");

  // Cấu hình tùy chọn cho MPU6050 (có thể bỏ qua ban đầu)
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G); // Dải đo gia tốc (+/- 8G)
  // Serial.print("Accelerometer range set to: ");
  // switch (mpu.getAccelerometerRange()) { /* ... (in ra range nếu cần) ... */ }

  mpu.setGyroRange(MPU6050_RANGE_500_DEG);    // Dải đo gyro (+/- 500 độ/s)
  // Serial.print("Gyro range set to: ");
  // switch (mpu.getGyroRange()) { /* ... (in ra range nếu cần) ... */ }

  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); // Bộ lọc (giảm nhiễu)
  // Serial.print("Filter bandwidth set to: ");
  // switch (mpu.getFilterBandwidth()) { /* ... (in ra filter nếu cần) ... */ }
  Serial.println("MPU6050 da cau hinh.");
  delay(100); // Chờ MPU ổn định

  // --- Khởi tạo Nút nhấn ---
  pinMode(BUTTON_PIN, INPUT_PULLUP); // Sử dụng điện trở kéo lên nội của ESP32
  Serial.println("Nut nhan da khoi tao (GPIO 12 PULLUP).");


  // ... (Phần khởi tạo module SIM như cũ) ...
}

void loop() {
  // 1. Xử lý nút nhấn đặt tham chiếu
  handleSetReferenceButton();

  // 2. Đọc và xử lý dữ liệu MPU6050 (chỉ khi đã đặt tham chiếu)
  if (referenceSet) {
      readAndProcessMPU();
  }

  // 3. Đọc và cập nhật dữ liệu GPS
  // updateGPS();

  // 4. Kiểm tra và xử lý dữ liệu từ SIM
  // checkAndProcessSIM();

  // --- Thêm các tác vụ chính khác của VDetect tại đây ---
  // ...

  delay(50); // Giữ delay nhỏ
}

/**
 * @brief Xử lý sự kiện nhấn giữ nút để đặt trục gốc tham chiếu.
 */
void handleSetReferenceButton() {
  int buttonState = digitalRead(BUTTON_PIN);

  // Phát hiện bắt đầu nhấn (chuyển từ HIGH sang LOW)
  if (buttonState == LOW && buttonPressStartTime == 0) {
    buttonPressStartTime = millis();
    settingReference = true;
    Serial.println("Nhan nut...");
    // Có thể thêm tiếng bíp ngắn ở đây để báo bắt đầu nhấn
    beep_led(1, 50);
  }
  // Kiểm tra nếu đang giữ nút và đủ thời gian
  else if (buttonState == LOW && settingReference) {
    if (millis() - buttonPressStartTime >= SET_REF_HOLD_TIME) {
      Serial.println("Da giu nut du thoi gian. Dang dat truc goc...");

      // Đọc giá trị cảm biến hiện tại để làm tham chiếu
      // Có thể đọc nhiều lần và lấy trung bình để ổn định hơn, nhưng tạm thời đọc 1 lần
      sensors_event_t a, g, temp;
      mpu.getEvent(&a, &g, &temp);

      // Lưu giá trị gia tốc làm tham chiếu
      refAx = a.acceleration.x;
      refAy = a.acceleration.y;
      refAz = a.acceleration.z;
      referenceSet = true; // Đánh dấu đã đặt tham chiếu thành công

      Serial.printf("=> Truc Goc Da Dat: Ax=%.2f, Ay=%.2f, Az=%.2f\n", refAx, refAy, refAz);

      // Phát tín hiệu báo thành công
      beep_led(2, 150); // Kêu 2 tiếng bíp dài hơn

      // Reset trạng thái nút nhấn để tránh lặp lại ngay lập tức nếu vẫn giữ
      settingReference = false;
      // Không reset buttonPressStartTime về 0 ngay, chỉ reset khi nhả nút
    }
  }
  // Phát hiện nhả nút
  else if (buttonState == HIGH && buttonPressStartTime != 0) {
    Serial.println("Nha nut.");
    buttonPressStartTime = 0; // Reset thời điểm nhấn
    settingReference = false;   // Reset cờ đang nhấn giữ
  }
}

/**
 * @brief Đọc MPU6050 và xác định trạng thái nghiêng dựa trên trục gốc.
 * LƯU Ý: Cách xác định này chỉ dựa vào gia tốc kế, sẽ không chính xác
 * khi xe đang chạy/phanh/tăng tốc hoặc rung lắc mạnh. Cần các thuật toán
 * phức tạp hơn (kết hợp gyro) để phát hiện tai nạn thực sự.
 */
void readAndProcessMPU() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Biến lưu trạng thái hiện tại, tránh in liên tục
  static String lastTiltStatus = "";
  String currentTiltStatus = "Dung Thang"; // Mặc định

  // --- Xác định trạng thái nghiêng ---
  // Quan trọng: Chiều nghiêng (Trái/Phải, Trước/Sau) phụ thuộc vào cách bạn lắp MPU6050 lên xe.
  // Giả định:
  // - Trục Y của MPU6050 song song với chiều ngang của xe. Ay dương là nghiêng phải.
  // - Trục X của MPU6050 song song với chiều dọc của xe. Ax dương là chúi về trước.
  // - Trục Z hướng lên trên khi xe đứng thẳng (refAz thường âm do trọng lực hướng xuống).

  // 1. Kiểm tra Lật Ngược (ưu tiên cao nhất)
  // Kiểm tra nếu Az đổi dấu so với refAz và vượt qua ngưỡng
  if (referenceSet && refAz != 0 && (a.acceleration.z * refAz < 0) && abs(a.acceleration.z) > abs(UPSIDE_DOWN_THRESHOLD_Z)) {
     currentTiltStatus = "Lat Nguoc";
  }
  // 2. Kiểm tra Nghiêng Trái/Phải (theo trục Y)
  else if (a.acceleration.y > refAy + TILT_THRESHOLD_ACCEL) {
      currentTiltStatus = "Nghieng Phai";
  } else if (a.acceleration.y < refAy - TILT_THRESHOLD_ACCEL) {
      currentTiltStatus = "Nghieng Trai";
  }
  // 3. Kiểm tra Nghiêng Trước/Sau (theo trục X)
  else if (a.acceleration.x > refAx + TILT_THRESHOLD_ACCEL) {
      currentTiltStatus = "Nga Ve Truoc";
  } else if (a.acceleration.x < refAx - TILT_THRESHOLD_ACCEL) {
      currentTiltStatus = "Nga Ve Sau";
  }
  // Nếu không rơi vào các trường hợp trên -> Đứng thẳng

  // Chỉ in ra Serial Monitor nếu trạng thái thay đổi
  if (currentTiltStatus != lastTiltStatus) {
      Serial.print("Trang thai MPU: ");
      Serial.println(currentTiltStatus);
      // In giá trị raw nếu cần debug:
      // Serial.printf("  Raw: Ax=%.2f, Ay=%.2f, Az=%.2f\n", a.acceleration.x, a.acceleration.y, a.acceleration.z);
      // Serial.printf("  Ref: Ax=%.2f, Ay=%.2f, Az=%.2f\n", refAx, refAy, refAz);
      lastTiltStatus = currentTiltStatus;
  }

  // Có thể thêm xử lý gyro ở đây để phát hiện va đập mạnh (gia tốc góc lớn)
  // Ví dụ: float gyroMagnitude = sqrt(sq(g.gyro.x) + sq(g.gyro.y) + sq(g.gyro.z));
  // if (gyroMagnitude > NGUONG_VA_DAP) { /* Xử lý va đập */ }
}
/**
 * @brief Bíp còi và nháy LED để báo hiệu.
 */
void beep_led(int count, int duration_ms) {
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(duration_ms);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    if (count > 1 && i < count - 1) {
      delay(duration_ms);
    }
  }
}