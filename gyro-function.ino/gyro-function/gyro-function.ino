#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h> // Cho các hàm toán học

// =========================================================================
// --- CẤU HÌNH ---
// =========================================================================

// --- Chân Kết Nối ---
#define LED_PIN                 19 // Chân LED (Mặc định ESP32 DevKitC thường là 2)
#define BUZZER_PIN              18 // Chân còi báo
#define MPU_REF_BUTTON_PIN      0 // Chân nút nhấn ĐẶT TRỤC GỐC MPU6050
#define ALERT_RESET_BUTTON_PIN  4  // Chân nút nhấn DỪNG CẢNH BÁO

// --- Cấu hình MPU6050 cho Phát hiện Tai nạn/Ngã ---
const float ACCEL_MAGNITUDE_DEVIATION_THRESHOLD = 2.5; // Ngưỡng độ lệch gia tốc để xác định bắt đầu chuyển động (m/s^2) 
const float GYRO_MAGNITUDE_THRESHOLD = 705.0;  // Ngưỡng tốc độ góc để xác định bắt đầu chuyển động (deg/s)
const unsigned long MOTION_STOP_DURATION = 2000; // Thời gian đứng yên để xác nhận dừng (ms)
const float SIGNIFICANT_TILT_THRESHOLD_ACCEL = 7.0; // Ngưỡng độ lệch gia tốc X hoặc Y so với gốc để coi là nghiêng đáng kể (m/s^2). Ví dụ ~45 độ.
const float PARKED_Z_LOW_THRESHOLD_RATIO = 0.5; // Tỉ lệ Az tối thiểu so với refAz để coi là còn đứng/nghiêng ít (50%)
const float MOVING_IMPACT_THRESHOLD_G = 1.5;    // Ngưỡng G-Force va chạm (6G). Cần va chạm RẤT MẠNH. TĂNG/GIẢM KHI TEST.

// --- Các cài đặt thời gian ---
const unsigned long SET_REF_HOLD_TIME = 3000;    // Thời gian nhấn giữ nút đặt gốc (ms)
const unsigned long ALERT_COOLDOWN = 2000;       // Thời gian chờ tối thiểu giữa các cảnh báo mới (ms)

// =========================================================================
// --- BIẾN TOÀN CỤC ---
// =========================================================================
Adafruit_MPU6050 mpu;
float refAx = 0.0, refAy = 0.0, refAz = 0.0; // Trục gốc tham chiếu
bool referenceSet = false;                   // Cờ báo đã đặt tham chiếu
unsigned long buttonPressStartTime = 0;      // Thời điểm nhấn nút đặt gốc
bool settingReference = false;               // Cờ đang nhấn giữ nút đặt gốc
bool isMoving = false;                       // Trạng thái đang di chuyển (tính theo MPU)
bool parkedFallDetected = false;             // Cờ báo đã phát hiện ngã khi đỗ
bool movingAccidentDetected = false;         // Cờ báo đã phát hiện tai nạn khi di chuyển
unsigned long lastAlertTime = 0;             // Thời điểm cảnh báo cuối cùng

// =========================================================================
// --- KHAI BÁO HÀM ---
// =========================================================================
void beep_led(int count, int duration_ms);
void initMPU6050();
void handleSetReferenceButton();
void checkAccidentAndFall();
void handleAlerts();

// =========================================================================
// --- SETUP ---
// =========================================================================
void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\n--- MPU6050 Accident/Fall Detection Test (v3 - Tilt Based) ---");

  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
  pinMode(MPU_REF_BUTTON_PIN, INPUT_PULLUP); // Nút đặt gốc (GPIO 12)
  pinMode(ALERT_RESET_BUTTON_PIN, INPUT_PULLUP); // Nút reset cảnh báo (GPIO 4)
  Serial.println("Pins Initialized.");

  initMPU6050(); // Khởi tạo MPU (Hàm này sẽ dừng nếu lỗi)

  // Nếu chạy đến đây là MPU OK
  Serial.println("MPU Ready. Hold button (GPIO 12) for 3s to set reference.");
  Serial.println("\n==== KHOI DONG HOAN TAT ====");
}

// =========================================================================
// --- LOOP ---
// =========================================================================
void loop() {
  handleSetReferenceButton(); // Xử lý nút đặt trục gốc
  checkAccidentAndFall();     // Kiểm tra tai nạn/ngã xe
  handleAlerts();             // Xử lý hiển thị cảnh báo và nút reset

  delay(100); // Loop delay (có thể giảm xuống 50 nếu cần phản ứng nhanh hơn)
}

// =========================================================================
// --- CÁC HÀM CHỨC NĂNG ---
// =========================================================================

/**
 * @brief Bíp còi và nháy LED để báo hiệu.
 */
void beep_led(int count, int duration_ms) {
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_PIN, HIGH); digitalWrite(BUZZER_PIN, HIGH); delay(duration_ms);
    digitalWrite(LED_PIN, LOW); digitalWrite(BUZZER_PIN, LOW);
    if (count > 1 && i < count - 1) { delay(duration_ms); }
  }
}

/**
 * @brief Khởi tạo cảm biến MPU6050.
 */
void initMPU6050() {
  Serial.println("Khoi tao I2C & MPU6050...");
  if (!Wire.begin()) { Serial.println("!!! LOI KHOI TAO I2C !!!"); while(1) delay(10); }
  if (!mpu.begin()) { Serial.println("!!! Khong tim thay chip MPU6050 !!!"); while (1) { beep_led(1, 500); delay(500); } }
  else {
      Serial.println("MPU6050 da tim thay!");
      mpu.setAccelerometerRange(MPU6050_RANGE_8_G); // Dải đo quan trọng
      mpu.setGyroRange(MPU6050_RANGE_500_DEG);    // Dải đo quan trọng
      mpu.setFilterBandwidth(MPU6050_BAND_44_HZ); // Bộ lọc nhiễu
      Serial.println("MPU6050 da cau hinh."); delay(100);
      isMoving = false; referenceSet = false;
  }
}

/**
 * @brief Xử lý sự kiện nhấn giữ nút (GPIO 12) để đặt trục gốc tham chiếu.
 */
void handleSetReferenceButton() {
  int buttonState = digitalRead(MPU_REF_BUTTON_PIN);
  if (buttonState == LOW && buttonPressStartTime == 0) { buttonPressStartTime = millis(); settingReference = true; Serial.println("Nhan nut dat truc goc..."); }
  else if (buttonState == LOW && settingReference) { if (millis() - buttonPressStartTime >= SET_REF_HOLD_TIME) { Serial.println("Da giu nut du thoi gian. Dang dat truc goc..."); sensors_event_t a, g, temp; mpu.getEvent(&a, &g, &temp); refAx = a.acceleration.x; refAy = a.acceleration.y; refAz = a.acceleration.z; if(abs(refAz) < 2.0) { Serial.println("CANH BAO: Truc goc co ve khong thang dung (Az gan 0)."); beep_led(3, 100); } else { beep_led(2, 150); } referenceSet = true; Serial.printf("=> Truc Goc Da Dat: Ax=%.2f, Ay=%.2f, Az=%.2f\n", refAx, refAy, refAz); settingReference = false; } }
  else if (buttonState == HIGH && buttonPressStartTime != 0) { buttonPressStartTime = 0; settingReference = false; }
}

/**
 * @brief Kiểm tra trạng thái ngã xe khi đỗ và tai nạn khi di chuyển bằng MPU6050.
 * *Đã cập nhật logic phát hiện và báo hiệu theo yêu cầu mới*
 */
void checkAccidentAndFall() {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp); // Đọc dữ liệu MPU

    // --- Xác định trạng thái Di chuyển / Đứng yên (CHỈ DỰA VÀO MPU) ---
    float accelMag = sqrt(sq(a.acceleration.x) + sq(a.acceleration.y) + sq(a.acceleration.z));
    float gx_dps = g.gyro.x * 180.0 / M_PI; float gy_dps = g.gyro.y * 180.0 / M_PI; float gz_dps = g.gyro.z * 180.0 / M_PI;
    float gyroMag = sqrt(sq(gx_dps) + sq(gy_dps) + sq(gz_dps));
    bool mpuActive = (abs(accelMag - 9.81) > ACCEL_MAGNITUDE_DEVIATION_THRESHOLD) || (gyroMag > GYRO_MAGNITUDE_THRESHOLD);
    static unsigned long firstStillTimestamp_internal = 0;

    if (mpuActive) {
        firstStillTimestamp_internal = 0;
        if (!isMoving) {
            Serial.println("MPU -> Moving (Detected Start)");
            isMoving = true;
            digitalWrite(LED_PIN, HIGH); // Bật LED khi di chuyển
            beep_led(2, 100); // Kêu/nháy nhanh 2 lần <<< THAY ĐỔI
        }
    } else {
        if (isMoving) {
            if (firstStillTimestamp_internal == 0) {
                firstStillTimestamp_internal = millis();
            } else if (millis() - firstStillTimestamp_internal >= MOTION_STOP_DURATION) {
                 if(isMoving) {
                    Serial.println("MPU -> Stopped (Confirmed)");
                    digitalWrite(LED_PIN, LOW); // Tắt LED khi dừng hẳn <<< THAY ĐỔI
                    // Không kêu bíp khi dừng
                 }
                 isMoving = false;
                 firstStillTimestamp_internal = 0;
            }
        } else { // Đã đứng yên và vẫn đứng yên
             firstStillTimestamp_internal = 0;
             digitalWrite(LED_PIN, LOW); // Đảm bảo LED tắt
        }
    }
    // --- Kết thúc xác định trạng thái Di chuyển / Đứng yên ---

    // --- Chỉ kiểm tra cảnh báo nếu đã qua thời gian cooldown ---
    if (millis() - lastAlertTime < ALERT_COOLDOWN) {
        return;
    }

    // --- Tính toán các yếu tố chính để phát hiện ---
    bool highImpact = (accelMag > (MOVING_IMPACT_THRESHOLD_G * 9.81)); // Có va chạm mạnh không?
    bool significantTilt = false;
    if (referenceSet) { // Chỉ tính nghiêng nếu đã đặt gốc
        float deltaX = abs(a.acceleration.x - refAx); float deltaY = abs(a.acceleration.y - refAy);
        bool significantTiltZ = (refAz != 0) && (abs(a.acceleration.z) < abs(refAz * PARKED_Z_LOW_THRESHOLD_RATIO) || (a.acceleration.z * refAz < 0 && abs(a.acceleration.z) > 1.0) );
        significantTilt = (deltaX > SIGNIFICANT_TILT_THRESHOLD_ACCEL || deltaY > SIGNIFICANT_TILT_THRESHOLD_ACCEL) && significantTiltZ;
    }

    // --- Logic Phát Hiện Ngã Xe Khi Đang Đỗ ---
    // Điều kiện: Không di chuyển VÀ có nghiêng đáng kể VÀ KHÔNG có va chạm mạnh đồng thời VÀ chưa báo ngã
    if (!isMoving && significantTilt && !highImpact && !parkedFallDetected && !movingAccidentDetected) {
        Serial.println(">>> PHAT HIEN: NGA XE KHI DO (Nghieng nhung khong va cham)! <<<");
        Serial.printf("    Accel: X=%.2f, Y=%.2f, Z=%.2f (Ref: X=%.2f, Y=%.2f, Z=%.2f)\n", a.acceleration.x, a.acceleration.y, a.acceleration.z, refAx, refAy, refAz);
        parkedFallDetected = true;      // Bật cờ
        lastAlertTime = millis();
        // *** THAY ĐỔI: Kêu bíp 4 lần NGAY LẬP TỨC ***
        beep_led(4, 100); // 4 lần bíp/nháy nhanh rồi thôi
    } else if (isMoving && parkedFallDetected) {
         Serial.println("Xe di chuyen -> Xoa trang thai nga khi do."); parkedFallDetected = false; // Reset cờ ngã
    }

    // --- Logic Phát Hiện Tai Nạn Khi Đang Di Chuyển ---
    // Điều kiện: Đang di chuyển VÀ có va chạm mạnh ĐỒNG THỜI có nghiêng đáng kể VÀ chưa báo tai nạn
    if (isMoving && highImpact && significantTilt && !movingAccidentDetected && !parkedFallDetected) {
         Serial.println(">>> PHAT HIEN: TAI NAN KHI DI CHUYEN (Va cham MANH + Nghieng)! <<<");
         Serial.printf("    AccelMag: %.2f (G=%.2f). Accel: X=%.2f, Y=%.2f, Z=%.2f\n", accelMag, accelMag/9.81, a.acceleration.x, a.acceleration.y, a.acceleration.z);
         movingAccidentDetected = true;   // Bật cờ -> handleAlerts sẽ xử lý cảnh báo liên tục
         lastAlertTime = millis();
    }
}

/**
 * @brief Xử lý hiển thị cảnh báo (chỉ cho tai nạn di chuyển) và nút reset.
 * *Đã cập nhật để chỉ cảnh báo liên tục cho movingAccidentDetected*
 */
void handleAlerts() {
    static unsigned long alertButtonHoldStart = 0;
    static bool resetButtonWasPressed = false;

    // --- Chỉ xử lý cảnh báo LIÊN TỤC cho Tai Nạn Khi Di Chuyển ---
    if (movingAccidentDetected) {
        // --- Điều khiển LED/Còi báo động Tai nạn (nháy nhanh liên tục) ---
        unsigned long interval = 250; unsigned long onDuration = 125;
        if ((millis() % interval) < onDuration) { digitalWrite(BUZZER_PIN, HIGH); digitalWrite(LED_PIN, HIGH); }
        else { digitalWrite(BUZZER_PIN, LOW); digitalWrite(LED_PIN, LOW); }

        // --- Kiểm tra nút nhấn (GPIO 4) để tắt cảnh báo ---
        bool isResetButtonPressed = (digitalRead(ALERT_RESET_BUTTON_PIN) == LOW);
        if (isResetButtonPressed) {
            if (alertButtonHoldStart == 0) alertButtonHoldStart = millis();
            else if (millis() - alertButtonHoldStart > 1000) { // Giữ 1 giây
                Serial.println("!!! Canh bao Tai nan/Nga xe da duoc TAT BANG NUT NHAN !!!");
                movingAccidentDetected = false; parkedFallDetected = false; // Tắt cả 2 cờ
                lastAlertTime = millis(); alertButtonHoldStart = 0;
                digitalWrite(BUZZER_PIN, LOW); digitalWrite(LED_PIN, LOW); // Tắt ngay
            }
        } else { alertButtonHoldStart = 0; } // Reset khi nhả nút
    }
    // --- Xử lý khi KHÔNG có cảnh báo Tai Nạn Di Chuyển ---
    else {
        // Đảm bảo Còi luôn tắt nếu không có cảnh báo tai nạn
        digitalWrite(BUZZER_PIN, LOW);

        // LED sẽ được điều khiển bởi trạng thái isMoving (Sáng khi chạy, Tắt khi dừng)
        if (!isMoving) {
            digitalWrite(LED_PIN, LOW);
        } else {
             digitalWrite(LED_PIN, HIGH);
        }

        // Xử lý nút nhấn nhanh để tắt cờ parkedFallDetected (nếu nó đang bật)
        bool isResetButtonCurrentlyPressed = (digitalRead(ALERT_RESET_BUTTON_PIN) == LOW);
        // Chỉ tắt cờ khi nút VỪA ĐƯỢC NHẤN (tránh tắt liên tục khi giữ)
        if(parkedFallDetected && isResetButtonCurrentlyPressed && !resetButtonWasPressed) {
              Serial.println("!!! Canh bao Nga khi do da duoc XOA BANG NUT NHAN !!!");
              parkedFallDetected = false;
              lastAlertTime = millis(); // Reset cooldown
        }
        resetButtonWasPressed = isResetButtonCurrentlyPressed; // Lưu trạng thái nút cho lần kiểm tra sau
    }
}

// --- Hàm clearEEPROM (Không cần thiết trong code này) ---
// void clearEEPROM() { /* ... */ }