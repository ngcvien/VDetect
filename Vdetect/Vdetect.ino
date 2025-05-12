// VDetect.ino

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>  // Cho các hàm toán học

#include "notifications.h"  // QUAN TRỌNG: Bao gồm file header của bạn

// =========================================================================
// --- CẤU HÌNH CHO PHẦN MPU TRONG FILE NÀY ---
// =========================================================================

// --- Chân Kết Nối (MPU và các thành phần điều khiển từ .ino) ---
// !!! KIỂM TRA LẠI CÁC CHÂN NÀY CHO ĐÚNG VỚI MẠCH CỦA BẠN !!!
#define MPU_LED_PIN 19                // Chân LED (Lấy từ file gyro.cpp của bạn)
#define MPU_BUZZER_PIN 18             // Chân Còi (Lấy từ file gyro.cpp của bạn)
#define MPU_REF_BUTTON_PIN 0          // Chân nút nhấn ĐẶT TRỤC GỐC MPU6050 (LƯU Ý CẢNH BÁO GPIO 0!)
#define MPU_ALERT_RESET_BUTTON_PIN 4  // Chân nút nhấn DỪNG CẢNH BÁO MPU / Dừng Tìm Xe

// --- Ngưỡng MPU6050 (Đã điều chỉnh một số giá trị) ---
const float ACCEL_MAGNITUDE_DEVIATION_THRESHOLD = 2.5;  // Ngưỡng gia tốc xác định bắt đầu chuyển động
const float GYRO_MAGNITUDE_THRESHOLD = 750.0;           // Ngưỡng gyro xác định bắt đầu chuyển động (SỬA TỪ 705.0)
const unsigned long MOTION_STOP_DURATION = 2000;        // Thời gian đứng yên để xác nhận dừng
const float SIGNIFICANT_TILT_THRESHOLD_ACCEL = 7.0;     // Ngưỡng lệch gia tốc X/Y cho nghiêng đáng kể
const float PARKED_Z_LOW_THRESHOLD_RATIO = 0.5;         // Tỉ lệ Az tối thiểu so với refAz
const float MOVING_IMPACT_THRESHOLD_G = 2.0;            // Ngưỡng G-Force va chạm (SỬA TỪ 1.5G)

// --- Thời gian MPU ---
const unsigned long SET_REF_HOLD_TIME = 3000;
const unsigned long ALERT_COOLDOWN = 5000;  // Thời gian chờ giữa các cảnh báo (SỬA TỪ 2000ms)

// --- Biến Toàn Cục MPU ---
Adafruit_MPU6050 mpu;
float refAx = 0.0, refAy = 0.0, refAz = 0.0;
bool referenceSet = false;
unsigned long buttonPressStartTime = 0;
bool settingReference = false;
bool isMoving_MPU = false;  // Trạng thái di chuyển riêng của MPU (để LED MPU hoạt động độc lập)
bool parkedFallDetected_MPU = false;
bool movingAccidentDetected_MPU = false;
unsigned long lastAlertTime_MPU = 0;

// --- KHAI BÁO HÀM CỤC BỘ CHO MPU ---
void initMPU6050_local();  // Đổi tên để tránh xung đột nếu beep_led_notifications cũng có tên này
void handleSetReferenceButton_MPU();
void checkAccidentAndFall_MPU();
void handleAlerts_MPU();

// =========================================================================
// --- SETUP ---
// =========================================================================
void setup() {
  Serial.begin(115200);
  while (!Serial)
    ;
  Serial.println("\n\n\n-----------------------\nVDetect - Main System Starting...");
  if (MPU_REF_BUTTON_PIN == 0) {
    Serial.println("!!! CANH BAO: Nut Dat Truc Goc MPU dang dung GPIO 0. KHONG NHAN GIU khi khoi dong/reset ESP32 !!!");
  }

  pinMode(MPU_LED_PIN, OUTPUT);
  digitalWrite(MPU_LED_PIN, LOW);
  pinMode(MPU_BUZZER_PIN, OUTPUT);
  digitalWrite(MPU_BUZZER_PIN, LOW);
  pinMode(MPU_REF_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MPU_ALERT_RESET_BUTTON_PIN, INPUT_PULLUP);
  Serial.println("MPU Control Pins Initialized.");

  initMPU6050_local();  // Khởi tạo MPU

  // Khởi tạo tất cả các module SIM, GPS, EEPROM từ notifications.cpp
  // EEPROM.begin() sẽ được gọi trong initNotifications qua loadContacts
  if (!EEPROM.begin(EEPROM_SIZE_NOTIF)) {  // Sử dụng define từ notifications.h
    Serial.println("!!! LOI KHOI TAO EEPROM (main) !!!");
    while (true) {
      delay(1000);
    }
  }
  initNotifications();  // Gọi hàm khởi tạo từ notifications.cpp, truyền Serial để log

  Serial.println("\n==== KHOI DONG HE THONG HOAN TAT ====");
  // Kiểm tra MPU sau initMPU6050 (hàm initMPU6050 đã có loop vô tận nếu lỗi)
  Serial.println("MPU Ready. Hold MPU Ref Button (GPIO 0) for 3s to set reference.");
}

// =========================================================================
// --- LOOP ---
// =========================================================================
void loop() {
  handleSetReferenceButton_MPU();  // Xử lý nút đặt trục gốc MPU
  checkAccidentAndFall_MPU();      // Kiểm tra tai nạn/ngã xe bằng MPU

  updateGPS_notifications();        // Cập nhật dữ liệu GPS
  checkAndProcessCommunications();  // Xử lý SIM/SMS/Call (sẽ tự kích hoạt FindMeAlarm nếu cần)

  handleAlerts_MPU();  // Xử lý hiển thị cảnh báo MPU và FindMe Alarm

  // Logic kết hợp: Nếu MPU phát hiện tai nạn/ngã, thì gọi hàm gửi SMS từ notifications
  static bool alertSmsSent_MPU = false;  // Cờ riêng cho SMS cảnh báo MPU
  if (parkedFallDetected_MPU || movingAccidentDetected_MPU) {
    if (!alertSmsSent_MPU && (millis() - lastAlertTime_MPU < ALERT_COOLDOWN + 1000)) {  // Chỉ gửi trong khoảng cooldown + 1s
      Serial.println("MPU detected event, preparing to send SMS alert...");
      String alertType = parkedFallDetected_MPU ? "NGA XE KHI DO" : String(getUserName_notifications())+" CO THE DA GAP TAI NAN KHI DI CHUYEN";
      char message[100];
      sprintf(message, "CANH BAO %s!", alertType.c_str());

      const char *p1 = getPhone1Num_notifications();
      if (p1 && p1[0] != '\0') {
        char fullMessage[250];
        if (isGPSFixed_notifications()) {
          // Tăng buffer
          sprintf(fullMessage, "%s Vi tri: https://maps.google.com/maps?q=%.6f,%.6f", message, getGPSLat_notifications(), getGPSLng_notifications());
          Serial.println(getGPSLat_notifications());
          Serial.println(getGPSLng_notifications());
          // Dùng sendErrorSMS_notifications để có prefix "VDetect Loi:", hoặc tạo hàm sendAlertSMS riêng
          sendErrorSMS_notifications(p1, fullMessage);
        } else {
          sprintf(fullMessage, "%s Vi tri khong xac dinh", message);
          sendErrorSMS_notifications(p1, fullMessage);
        }
        alertSmsSent_MPU = true;
      }
      // Gửi thêm cho phone2, phone3 nếu cần
    }
    // Nút reset trong handleAlerts_MPU sẽ reset parkedFallDetected_MPU, movingAccidentDetected_MPU
    // và nên reset cả alertSmsSent_MPU khi đó
    if (!parkedFallDetected_MPU && !movingAccidentDetected_MPU) {
      alertSmsSent_MPU = false;
    }
  } else {
    alertSmsSent_MPU = false;  // Reset cờ nếu không có cảnh báo MPU
  }

  delay(100);
}

// =========================================================================
// --- CÁC HÀM LIÊN QUAN ĐẾN MPU6050 (Lấy từ gyro.cpp và điều chỉnh) ---
// =========================================================================

void initMPU6050_local() {
  Serial.println("Khoi tao I2C & MPU6050 (local)...");
  if (!Wire.begin()) {
    Serial.println("!!! LOI KHOI TAO I2C !!!");
    while (1)
      delay(10);
  }
  if (!mpu.begin()) {
    Serial.println("!!! Khong tim thay chip MPU6050 !!!");
    while (1) {
      beep_led_notifications(1, 500);
      delay(500);
    }
  } else {
    Serial.println("MPU6050 da tim thay!");
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);
    Serial.println("MPU6050 da cau hinh.");
    delay(100);
    isMoving_MPU = false;
    referenceSet = false;
  }
}

void handleSetReferenceButton_MPU() {
  int buttonState = digitalRead(MPU_REF_BUTTON_PIN);
  if (buttonState == LOW && buttonPressStartTime == 0) {
    buttonPressStartTime = millis();
    settingReference = true;
    Serial.println("Nhan nut dat truc goc (MPU)...");
  } else if (buttonState == LOW && settingReference) {
    if (millis() - buttonPressStartTime >= SET_REF_HOLD_TIME) {
      Serial.println("Da giu nut du thoi gian. Dang dat truc goc (MPU)...");
      sensors_event_t a, g, temp;
      mpu.getEvent(&a, &g, &temp);
      refAx = a.acceleration.x;
      refAy = a.acceleration.y;
      refAz = a.acceleration.z;
      if (abs(refAz) < 6.0) {
        Serial.println("CANH BAO (MPU): Truc goc co ve khong thang dung (Az gan 0).");
        beep_led_notifications(3, 100);
      } else {
        beep_led_notifications(2, 150);
      }
      referenceSet = true;
      Serial.printf("=> Truc Goc MPU Da Dat: Ax=%.2f, Ay=%.2f, Az=%.2f\n", refAx, refAy, refAz);
      settingReference = false;
    }
  } else if (buttonState == HIGH && buttonPressStartTime != 0) {
    buttonPressStartTime = 0;
    settingReference = false;
  }
}

void checkAccidentAndFall_MPU() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float accelMag = sqrt(sq(a.acceleration.x) + sq(a.acceleration.y) + sq(a.acceleration.z));
  float gx_dps = g.gyro.x * 180.0 / M_PI;
  float gy_dps = g.gyro.y * 180.0 / M_PI;
  float gz_dps = g.gyro.z * 180.0 / M_PI;
  float gyroMag = sqrt(sq(gx_dps) + sq(gy_dps) + sq(gz_dps));
  bool mpuActive = (abs(accelMag - 9.81) > ACCEL_MAGNITUDE_DEVIATION_THRESHOLD) || (gyroMag > GYRO_MAGNITUDE_THRESHOLD);
  static unsigned long firstStillTimestamp_internal = 0;

  if (mpuActive) {
    firstStillTimestamp_internal = 0;
    if (!isMoving_MPU) {
      Serial.println("MPU -> Moving (Detected Start)");
      isMoving_MPU = true;
      digitalWrite(MPU_LED_PIN, HIGH);
      beep_led_notifications(2, 100);
    }
  } else {
    if (isMoving_MPU) {
      if (firstStillTimestamp_internal == 0)
        firstStillTimestamp_internal = millis();
      else if (millis() - firstStillTimestamp_internal >= MOTION_STOP_DURATION) {
        if (isMoving_MPU) {
          Serial.println("MPU -> Stopped (Confirmed)");
          digitalWrite(MPU_LED_PIN, LOW);
        }
        isMoving_MPU = false;
        firstStillTimestamp_internal = 0;
      }
    } else {
      firstStillTimestamp_internal = 0;
      digitalWrite(MPU_LED_PIN, LOW);
    }
  }

  if (millis() - lastAlertTime_MPU < ALERT_COOLDOWN)
    return;

  bool highImpact = (accelMag > (MOVING_IMPACT_THRESHOLD_G * 9.81));
  bool significantTilt = false;
  if (referenceSet) {
    float deltaX = abs(a.acceleration.x - refAx);
    float deltaY = abs(a.acceleration.y - refAy);
    bool significantTiltZ = (refAz != 0) && (abs(a.acceleration.z) < abs(refAz * PARKED_Z_LOW_THRESHOLD_RATIO) || (a.acceleration.z * refAz < 0 && abs(a.acceleration.z) > 1.0));
    significantTilt = (deltaX > SIGNIFICANT_TILT_THRESHOLD_ACCEL || deltaY > SIGNIFICANT_TILT_THRESHOLD_ACCEL) && significantTiltZ;
  }

  if (!isMoving_MPU && significantTilt && !highImpact && !parkedFallDetected_MPU && !movingAccidentDetected_MPU) {
    Serial.println(">>> MPU PHAT HIEN: NGA XE KHI DO! <<<");
    Serial.printf("    Accel: X=%.2f, Y=%.2f, Z=%.2f (Ref: X=%.2f, Y=%.2f, Z=%.2f)\n", a.acceleration.x, a.acceleration.y, a.acceleration.z, refAx, refAy, refAz);
    parkedFallDetected_MPU = true;
    lastAlertTime_MPU = millis();
    beep_led_notifications(4, 100);
  } else if (isMoving_MPU && parkedFallDetected_MPU) {
    Serial.println("MPU: Xe di chuyen -> Xoa trang thai nga khi do.");
    parkedFallDetected_MPU = false;
  }

  if (isMoving_MPU && highImpact && significantTilt && !movingAccidentDetected_MPU && !parkedFallDetected_MPU) {
    Serial.println(">>> MPU PHAT HIEN: TAI NAN KHI DI CHUYEN! <<<");
    Serial.printf("    AccelMag: %.2f (G=%.2f). Accel: X=%.2f, Y=%.2f, Z=%.2f\n", accelMag, accelMag / 9.81, a.acceleration.x, a.acceleration.y, a.acceleration.z);
    movingAccidentDetected_MPU = true;
    lastAlertTime_MPU = millis();
  }
}

/**
 * @brief Xử lý hiển thị cảnh báo MPU và FindMe, và nút reset.
 */
void handleAlerts_MPU() {
  static unsigned long alertButtonHoldStart = 0;
  static bool resetButtonWasPressed = false;

  bool findMeState = isFindMeAlarmActive_notifications();  // Lấy trạng thái Tìm Xe

  if (movingAccidentDetected_MPU || findMeState) {                    // Cảnh báo liên tục cho tai nạn HOẶC Tìm Xe
    unsigned long interval = movingAccidentDetected_MPU ? 250 : 500;  // Tai nạn nháy nhanh hơn
    unsigned long onDuration = movingAccidentDetected_MPU ? 125 : 250;

    if ((millis() % interval) < onDuration) {
      digitalWrite(MPU_BUZZER_PIN, HIGH);
      digitalWrite(MPU_LED_PIN, HIGH);
    } else {
      digitalWrite(MPU_BUZZER_PIN, LOW);
      digitalWrite(MPU_LED_PIN, LOW);
    }

    // Nút reset tắt tất cả các loại cảnh báo liên tục
    bool isResetButtonPressed = (digitalRead(MPU_ALERT_RESET_BUTTON_PIN) == LOW);
    if (isResetButtonPressed) {
      if (alertButtonHoldStart == 0)
        alertButtonHoldStart = millis();
      else if (millis() - alertButtonHoldStart > 1000) {  // Giữ 1 giây
        Serial.println("!!! Canh bao (Tai nan/Nga/TimXe) da duoc TAT BANG NUT NHAN !!!");
        if (movingAccidentDetected_MPU)
          movingAccidentDetected_MPU = false;
        if (parkedFallDetected_MPU)
          parkedFallDetected_MPU = false;
        if (findMeState)
          setFindMeAlarmActive_notifications(false);  // Tắt cờ Tìm Xe

        lastAlertTime_MPU = millis();
        alertButtonHoldStart = 0;
        digitalWrite(MPU_BUZZER_PIN, LOW);
        digitalWrite(MPU_LED_PIN, LOW);
      }
    } else {
      alertButtonHoldStart = 0;
    }
  }
  // Xử lý khi KHÔNG có cảnh báo Tai Nạn Di Chuyển hoặc Tìm Xe đang hoạt động
  else {
    digitalWrite(MPU_BUZZER_PIN, LOW);  // Còi luôn tắt nếu không có cảnh báo liên tục

    // LED hiển thị trạng thái di chuyển của MPU
    if (!isMoving_MPU) {
      digitalWrite(MPU_LED_PIN, LOW);
    } else {
      digitalWrite(MPU_LED_PIN, HIGH);
    }

    // Nút nhấn nhanh để xóa cờ ngã khi đỗ (nếu nó đang bật và không có cảnh báo tai nạn)
    bool isResetButtonCurrentlyPressed = (digitalRead(MPU_ALERT_RESET_BUTTON_PIN) == LOW);
    if (parkedFallDetected_MPU && isResetButtonCurrentlyPressed && !resetButtonWasPressed) {
      Serial.println("!!! Canh bao MPU (Nga khi do) da duoc XOA BANG NUT NHAN !!!");
      parkedFallDetected_MPU = false;
      lastAlertTime_MPU = millis();
    }
    resetButtonWasPressed = isResetButtonCurrentlyPressed;
  }
}