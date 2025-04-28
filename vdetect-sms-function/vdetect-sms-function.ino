#include <Arduino.h>
#include <HardwareSerial.h>
#include <EEPROM.h>
#include <TinyGPS++.h> // Thư viện xử lý GPS

// =========================================================================
// --- CẤU HÌNH ---
// =========================================================================

// --- Chân Kết Nối ---
#define LED_PIN            19     // Chân LED (thay bằng LED_BUILTIN nếu dùng LED tích hợp)
#define BUZZER_PIN         18     // Chân còi báo
#define SIM_ENABLE_PIN     15     // Chân Enable/Reset cho module SIM (Kéo LOW để bật)
#define SIM_SERIAL_RX_PIN  26     // ESP32 RX <--- SIM TX
#define SIM_SERIAL_TX_PIN  27     // ESP32 TX ---> SIM RX
#define GPS_SERIAL_RX_PIN  16      // ESP32 RX1 <--- GPS TX (Chân mặc định UART1 RX) - **Sửa nếu bạn dùng chân khác**
#define GPS_SERIAL_TX_PIN  17     // ESP32 TX1 ---> GPS RX (Chân mặc định UART1 TX) - **Sửa nếu bạn dùng chân khác / Không bắt buộc nếu chỉ đọc**
#define FIND_ME_BUTTON_PIN 0      // Chân GPIO nối với nút nhấn dừng tìm xe
#define SerialAT Serial2 // Dùng UART2 của ESP32: TX = GPIO17, RX = GPIO16

bool findMeAlarmActive = false;   // Cờ báo hiệu trạng thái báo động Tìm Xe đang BẬT
String incomingCallerNumber = ""; // Lưu tạm số điện thoại đang gọi đến
// --- Module SIM ---
#define SIM_BAUD_RATE      115200 // Baud rate SIM
HardwareSerial simSerial(2);      // Dùng Serial2 cho SIM

// --- Module GPS ---
#define GPS_BAUD_RATE      9600   // Baud rate GPS (thường là 9600 cho NEO-6M)
HardwareSerial gpsSerial(1);      // Dùng Serial1 cho GPS - **Sửa số Serial nếu bạn dùng UART khác**
TinyGPSPlus gps;                  // Đối tượng TinyGPS++

// --- EEPROM ---
#define EEPROM_SIZE        200    // Kích thước EEPROM
#define EEPROM_MAGIC_ADDR  0      // Địa chỉ Magic Byte
#define EEPROM_DATA_ADDR   1      // Địa chỉ bắt đầu dữ liệu
#define EEPROM_MAGIC_VALUE 0xAB   // Giá trị Magic Byte

// --- Khác ---
const int PHONE_NUMBER_SIZE = 15; // Kích thước SĐT
const unsigned long SIM_INIT_TIMEOUT = 30000; // Timeout khởi tạo SIM
const unsigned long SIM_PIN_TIMEOUT = 15000;  // Timeout chờ PIN

// =========================================================================
// --- BIẾN TOÀN CỤC ---
// =========================================================================

// --- Danh bạ ---
char phone1[PHONE_NUMBER_SIZE] = ""; // Số chủ nhân
char phone2[PHONE_NUMBER_SIZE] = "";
char phone3[PHONE_NUMBER_SIZE] = "";
char zalo1[PHONE_NUMBER_SIZE] = "";
char zalo2[PHONE_NUMBER_SIZE] = "";
char zalo3[PHONE_NUMBER_SIZE] = "";

// --- Trạng thái ---
bool isRinging = false; // Trạng thái cuộc gọi

// --- Dữ liệu GPS ---
float currentLat = 0.0;   // Vĩ độ
float currentLng = 0.0;   // Kinh độ
bool gpsFix = false;      // Trạng thái có tín hiệu GPS hợp lệ
unsigned long lastGpsRead = 0; // Thời điểm đọc GPS hợp lệ cuối cùng

// --- Buffers ---
char smsBuffer[512];    // Buffer đọc nội dung SMS (cho parseSMSContent)

// =========================================================================
// --- KHAI BÁO HÀM ---
// =========================================================================

void beep_led(int count, int duration_ms = 100);
String sendCommand(const char* cmd, unsigned long timeout = 3000, bool requireOK = true, bool printResponse = true);
String readSerial(unsigned long timeout);
bool initializeSIM();
void loadContactsFromEEPROM();
bool saveContactsToEEPROM();
bool parseSMSContent(const char* smsContent, const char* sender);
void sendConfirmationSMS(const char* recipient);
void checkAndProcessSIM();
void clearEEPROM();
void updateGPS(); // Hàm đọc và xử lý GPS
void sendLocationSMS(const char* recipient); // Hàm gửi SMS vị trí


// =========================================================================
// --- SETUP ---
// =========================================================================
void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\n\n\n-----------------------\nVDetect - Khoi dong...");

  // --- Khởi tạo Outputs ---
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
   // --- THÊM: Khởi tạo nút nhấn dừng tìm xe ---
  pinMode(FIND_ME_BUTTON_PIN, INPUT_PULLUP); // Dùng điện trở kéo lên nội
  Serial.println("Nut nhan Dung Tim Xe da khoi tao (GPIO 4 PULLUP).");
  // --- Kích hoạt Chân Enable SIM ---
  pinMode(SIM_ENABLE_PIN, OUTPUT);
  digitalWrite(SIM_ENABLE_PIN, LOW);
  Serial.println("Da kich hoat chan Enable SIM (GPIO 15 LOW).");

  // --- Khởi tạo EEPROM ---
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("!!! LOI KHOI TAO EEPROM !!!");
    while (true);
  }
  Serial.println("EEPROM khoi tao thanh cong.");
  loadContactsFromEEPROM(); // Tải danh bạ đã lưu
  Serial.println("Danh ba da tai tu EEPROM:");
  Serial.printf("  Phone1: %s\n", phone1); // Quan trọng: Phải load được phone1
  Serial.printf("  Phone2: %s\n", phone2);
  Serial.printf("  Phone3: %s\n", phone3);
  Serial.printf("  Zalo1: %s\n", zalo1);
  Serial.printf("  Zalo2: %s\n", zalo2);
  Serial.printf("  Zalo3: %s\n", zalo3);

  // --- Khởi tạo Serial SIM ---
  Serial.println("Khoi tao Serial2 cho SIM...");
  simSerial.begin(SIM_BAUD_RATE, SERIAL_8N1, SIM_SERIAL_RX_PIN, SIM_SERIAL_TX_PIN);

  // --- Khởi tạo Serial cho GPS ---
  Serial.println("Khoi tao Serial1 cho GPS...");
  gpsSerial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_SERIAL_RX_PIN, GPS_SERIAL_TX_PIN); // Khởi tạo GPS Serial

  // --- Chờ Module SIM Khởi Động ---
  Serial.println("Cho module SIM khoi dong (5 giay)...");
  delay(5000);

  // --- Khởi tạo Module SIM ---
  Serial.println("Bat dau khoi tao module SIM (AT Commands)...");
  if (initializeSIM()) {
    Serial.println("****************************");
    Serial.println("* MODULE SIM DA SAN SANG! *");
    Serial.println("****************************");
    beep_led(1, 200);



    // --- Cấu hình SIM sau khi khởi tạo thành công ---
    sendCommand("AT+CMGF=1", 2000); delay(100); // Đặt chế độ SMS Text
    Serial.println("Dat che do thong bao SMS (CNMI=2,1)...");
    sendCommand("AT+CNMI=2,1,0,0,0", 2000); delay(100); // Yêu cầu +CMTI
    Serial.println("Kiem tra lai CNMI vua dat:");
    sendCommand("AT+CNMI?", 3000, false); delay(100);
    sendCommand("AT+CLIP=1", 2000); delay(100); // Bật Caller ID
    Serial.println("Dang xoa tin nhan cu tren SIM (neu co)...");
    sendCommand("AT+CMGD=1,4", 5000, true); delay(1000); // Xóa SMS cũ
    Serial.println("VDetect san sang nhan SMS cau hinh & Cuoc goi.");

  } else {
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("!!! LOI: KHONG KHOI TAO DUOC MODULE SIM !!!");
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    while(true) { beep_led(3, 50); delay(1000); } // Lặp báo lỗi
  }
}

// =========================================================================
// --- LOOP ---
// =========================================================================
void loop() {
  // 1. Liên tục đọc và cập nhật dữ liệu GPS
  updateGPS();

  // 2. Liên tục kiểm tra và xử lý dữ liệu từ SIM
  checkAndProcessSIM();

  // --- Thêm các tác vụ chính của VDetect tại đây ---
  // Ví dụ: Đọc cảm biến gia tốc, kiểm tra va chạm...
  // ...
  // --- Kết thúc tác vụ chính ---
  // 5. *** THÊM: Xử lý trạng thái báo động Tìm Xe (Còi + LED + Nút dừng) ***
  handleFindMeAlarm();

  while (Serial.available()) {
    simSerial.write(Serial.read());
  }

  // Truyền dữ liệu từ module -> máy tính
  while (SerialAT.available()) {
    Serial.write(simSerial.read());
  }
  delay(50); // Delay nhỏ trong loop chính
}

// =========================================================================
// --- CÁC HÀM CHỨC NĂNG ---
// =========================================================================

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

/**
 * @brief Đọc dữ liệu từ Serial của SIM với timeout, loại bỏ ký tự thừa.
 */
String readSerial(unsigned long timeout) {
  String response = "";
  unsigned long startTime = millis();
  while (millis() - startTime < timeout) {
    while (simSerial.available()) {
      char c = simSerial.read();
      if (c != '\r') { // Bỏ qua ký tự \r
         response += c;
      }
      startTime = millis(); // Reset timeout khi nhận được ký tự
    }
    if (response.length() > 0 && !simSerial.available()) {
       delay(50); // Chờ thêm 1 chút xíu xem còn gì nữa không
       if (!simSerial.available()) break;
    }
    delay(1); // Delay nhỏ tránh busy-waiting
  }
  response.trim(); // Xóa khoảng trắng, \n thừa ở đầu/cuối
  return response;
}


/**
 * @brief Gửi lệnh AT, chờ phản hồi và trả về chuỗi phản hồi.
 * @param cmd Lệnh AT cần gửi.
 * @param timeout Thời gian chờ tối đa (ms).
 * @param requireOK true: Phải có "OK" trong phản hồi để thành công. false: Chỉ cần có phản hồi.
 * @param printResponse true: In phản hồi ra Serial Monitor.
 * @return Chuỗi String phản hồi nếu thành công, chuỗi rỗng "" nếu thất bại/timeout.
 */
String sendCommand(const char* cmd, unsigned long timeout /*= 3000*/, bool requireOK /*= true*/, bool printResponse /*= true*/) {
  if (printResponse) {
    Serial.printf("Gui lenh: %s\n", cmd);
  }
  while(simSerial.available()) simSerial.read(); // Xóa buffer nhận trước khi gửi
  simSerial.println(cmd);

  String response = readSerial(timeout); // Đọc phản hồi

  if (printResponse) {
     Serial.print("Phan hoi: ");
     Serial.println(response);
  }

  if (response.length() == 0) {
      if (printResponse) Serial.println(" >> Timeout! Khong co phan hoi.");
      return ""; // Trả về chuỗi rỗng nếu timeout
  }

  // Kiểm tra các lỗi phổ biến trước khi kiểm tra OK
  if (response.indexOf("ERROR") != -1 || response.indexOf("BUSY") != -1 || response.indexOf("NO CARRIER") != -1) {
       if (printResponse) Serial.println(" >> Phan hoi chua ERROR/BUSY/NO CARRIER.");
       // Nếu requireOK là true, thì lỗi này cũng coi như thất bại
       // Nếu requireOK là false, vẫn trả về response để hàm gọi tự xử lý lỗi
       return requireOK ? "" : response;
  }

  if (requireOK) {
      if (response.indexOf("OK") != -1) {
          return response; // Có "OK", trả về phản hồi
      } else {
          if (printResponse) Serial.println(" >> Loi: Khong tim thay 'OK' trong phan hoi.");
          return ""; // Không có "OK", trả về chuỗi rỗng
      }
  } else {
      // Nếu không yêu cầu OK, chỉ cần có phản hồi là trả về
      return response;
  }
}

/**
 * @brief Khởi tạo module SIM với các lệnh AT cần thiết.
 * @return true nếu khởi tạo thành công, false nếu thất bại.
 */
bool initializeSIM() {
  // 1. AT
  Serial.println("1. Kiem tra giao tiep co ban (AT)...");
  if (sendCommand("AT", 5000, false).length() == 0) {
      Serial.println(" >> Buoc 1 THAT BAI: Module khong phan hoi lenh AT.");
      return false;
  }
  Serial.println(" >> Buoc 1 OK: Module co phan hoi.");
  delay(100);

  // 2. ATE0
  Serial.println("2. Tat echo (ATE0)...");
  if (sendCommand("ATE0", 2000, true).length() == 0) {
      Serial.println(" >> Buoc 2 THAT BAI: Khong tat duoc echo.");
      // return false; // Có thể bỏ qua lỗi này
  } else {
       Serial.println(" >> Buoc 2 OK: Da tat echo.");
  }
  delay(100);

  // 4. AT+CPIN?
  Serial.println("4. Kiem tra trang thai SIM Card (AT+CPIN?)...");
  unsigned long startTime = millis();
  bool pinReady = false;
  while (millis() - startTime < SIM_PIN_TIMEOUT) {
    String response = sendCommand("AT+CPIN?", 2000, false, false); // Tắt log của sendCommand ở đây
    if (response.length() > 0) {
        Serial.print("   Phan hoi CPIN: "); Serial.println(response); // In log kiểm tra
        if (response.indexOf("READY") != -1) {
            pinReady = true;
            Serial.println(" >> Buoc 4 OK: Trang thai SIM la READY!");
            break;
        } else if (response.indexOf("SIM PIN") != -1) { Serial.println(" >> LOI: SIM yeu cau ma PIN!"); return false; }
        else if (response.indexOf("SIM PUK") != -1) { Serial.println(" >> LOI: SIM yeu cau ma PUK!"); return false; }
    } else { Serial.println("   Khong nhan duoc phan hoi AT+CPIN?"); }
    delay(1500);
  }
  if (!pinReady) { Serial.println(" >> Buoc 4 THAT BAI: SIM khong san sang (Timeout)."); return false; }
  delay(100);

  // 5. AT+CREG?
  Serial.println("5. Kiem tra dang ky mang (AT+CREG?)...");
  startTime = millis();
  bool registered = false;
  int retries = 0;
  while (millis() - startTime < SIM_INIT_TIMEOUT) {
    String response = sendCommand("AT+CREG?", 2000, false, false); // Tắt log của sendCommand
    if (response.length() > 0) {
      Serial.print("   Phan hoi CREG: "); Serial.println(response); // In log kiểm tra
      int firstComma = response.indexOf(',');
      if (firstComma != -1) {
          // Lấy ký tự ngay sau dấu phẩy (là stat)
          char statChar = response.charAt(firstComma + 1);
          int stat = statChar - '0'; // Chuyển ký tự số sang giá trị int

          // Kiểm tra kỹ hơn nếu có thêm thông tin (ví dụ: CREG: 2,1,"LAC","CellID",...)
          // int secondComma = response.indexOf(',', firstComma + 1);
          // if(secondComma != -1) {
          //    stat = response.substring(firstComma + 1, secondComma).toInt();
          // } else {
          //     stat = response.substring(firstComma + 1).toInt();
          // }

          if (stat == 1 || stat == 5) { // 1 = Registered home, 5 = Registered roaming
            registered = true;
            Serial.print(" >> Buoc 5 OK: Da dang ky mang (Trang thai: "); Serial.print(stat); Serial.println(")");
            break;
          } else { Serial.print(" >> Chua dang ky mang (Trang thai: "); Serial.print(stat); Serial.println("), dang cho..."); }
      } else {
          // Phản hồi CREG không đúng định dạng mong đợi?
           Serial.println("   Phan hoi CREG khong chua dau phay ','?");
      }
    } else { Serial.println("   Khong nhan duoc phan hoi AT+CREG?"); }
    delay( (retries < 5) ? 2500 : 5000 ); // Chờ lâu hơn nếu thử nhiều lần
    retries++;
  }
  if (!registered) {
    Serial.println(" >> Buoc 5 THAT BAI: Khong dang ky duoc mang (Timeout).");
    Serial.println("Kiem tra chat luong song (AT+CSQ):");
    sendCommand("AT+CSQ", 3000, false); // Kiểm tra sóng khi lỗi
    return false;
  }
  return true;
}

/**
 * @brief Tải danh bạ từ EEPROM vào các biến toàn cục.
 */
void loadContactsFromEEPROM() {
  Serial.println("Doc du lieu tu EEPROM...");
  if (!EEPROM.begin(EEPROM_SIZE)) {
     Serial.println("Loi EEPROM.begin() khi doc!");
     return; // Không thể tiếp tục nếu EEPROM lỗi
  }
  if (EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VALUE) {
    int currentAddr = EEPROM_DATA_ADDR;
    EEPROM.get(currentAddr, phone1); currentAddr += PHONE_NUMBER_SIZE;
    EEPROM.get(currentAddr, phone2); currentAddr += PHONE_NUMBER_SIZE;
    EEPROM.get(currentAddr, phone3); currentAddr += PHONE_NUMBER_SIZE;
    EEPROM.get(currentAddr, zalo1);  currentAddr += PHONE_NUMBER_SIZE;
    EEPROM.get(currentAddr, zalo2);  currentAddr += PHONE_NUMBER_SIZE;
    EEPROM.get(currentAddr, zalo3);  currentAddr += PHONE_NUMBER_SIZE;
    Serial.println("Da tai danh ba tu EEPROM.");
  } else {
    Serial.println("EEPROM trong hoac du lieu khong hop le. Danh ba se rong.");
    strcpy(phone1, ""); strcpy(phone2, ""); strcpy(phone3, "");
    strcpy(zalo1, ""); strcpy(zalo2, ""); strcpy(zalo3, "");
  }
  // EEPROM.end(); // Không cần end nếu dùng liên tục? Tùy core ESP32.
}

/**
 * @brief Lưu các biến danh bạ toàn cục vào EEPROM.
 * @return true nếu lưu thành công, false nếu lỗi.
 */
bool saveContactsToEEPROM() {
  Serial.println("Luu danh ba vao EEPROM...");
   if (!EEPROM.begin(EEPROM_SIZE)) { // Cần begin trước khi ghi
     Serial.println("Loi EEPROM.begin() khi luu!");
     return false;
  }
  int currentAddr = EEPROM_DATA_ADDR;
  EEPROM.put(currentAddr, phone1); currentAddr += PHONE_NUMBER_SIZE;
  EEPROM.put(currentAddr, phone2); currentAddr += PHONE_NUMBER_SIZE;
  EEPROM.put(currentAddr, phone3); currentAddr += PHONE_NUMBER_SIZE;
  EEPROM.put(currentAddr, zalo1);  currentAddr += PHONE_NUMBER_SIZE;
  EEPROM.put(currentAddr, zalo2);  currentAddr += PHONE_NUMBER_SIZE;
  EEPROM.put(currentAddr, zalo3);  currentAddr += PHONE_NUMBER_SIZE;
  EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);

  if (EEPROM.commit()) {
    Serial.println("Luu EEPROM thanh cong.");
    // EEPROM.end(); // Commit xong có thể end
    return true;
  } else {
    Serial.println("Loi: Luu EEPROM that bai!");
    // EEPROM.end();
    return false;
  }
}

/**
 * @brief Phân tích nội dung SMS để lấy số điện thoại/Zalo.
 * @param smsContent Nội dung tin nhắn SMS (chuỗi C-style).
 * @param sender Số điện thoại người gửi (chuỗi C-style).
 * @return true nếu xử lý thành công (có thể không có thay đổi), false nếu lỗi lưu EEPROM.
 */
bool parseSMSContent(const char* smsContent, const char* sender) {
  Serial.println("Phan tich noi dung SMS:");
  // Serial.println(smsContent); // In nội dung nếu cần debug
  bool dataChanged = false;
  char tempBuffer[PHONE_NUMBER_SIZE];
  const char* keys[] = {"Phone1:", "Phone2:", "Phone3:", "Zalo1:", "Zalo2:", "Zalo3:"};
  char* targets[] = {phone1, phone2, phone3, zalo1, zalo2, zalo3};

  // Tách SMS thành các dòng nếu nội dung SMS có nhiều dòng
  char* smsCopy = strdup(smsContent); // Tạo bản sao để strtok không làm hỏng bản gốc
  if (smsCopy == NULL) {
       Serial.println("Loi cap phat bo nho cho smsCopy!");
       return false; // Lỗi nghiêm trọng
  }
  char* line = strtok(smsCopy, "\n"); // Tách theo dấu xuống dòng

  while (line != NULL) {
      // Serial.print("Xu ly dong: "); Serial.println(line); // Debug từng dòng
      for (int i = 0; i < 6; ++i) {
          const char* p = strstr(line, keys[i]);
          if (p) {
              const char* numStart = p + strlen(keys[i]);
              while (*numStart == ' ') numStart++; // Bỏ qua khoảng trắng đầu
              // Đọc số, dừng lại ở khoảng trắng hoặc hết chuỗi
              int n = sscanf(numStart, "%14s", tempBuffer);
              if (n == 1) {
                   // Kiểm tra xem có khác giá trị cũ không
                  if (strncmp(targets[i], tempBuffer, PHONE_NUMBER_SIZE) != 0) {
                      strncpy(targets[i], tempBuffer, PHONE_NUMBER_SIZE - 1);
                      targets[i][PHONE_NUMBER_SIZE - 1] = '\0'; // Đảm bảo kết thúc chuỗi
                      dataChanged = true;
                      Serial.printf("  Cap nhat %s %s\n", keys[i], targets[i]);
                  } else {
                      // Serial.printf("  Giu nguyen %s %s\n", keys[i], targets[i]);
                  }
                   // Đã tìm thấy key trên dòng này, xử lý dòng tiếp theo
                   goto next_line; // Nhảy ra khỏi vòng lặp for để lấy dòng mới
              } else {
                   Serial.printf("  Loi doc gia tri cho %s\n", keys[i]);
              }
          }
      }
      next_line:; // Nhãn để goto tới khi xử lý xong 1 key trên 1 dòng
      line = strtok(NULL, "\n"); // Lấy dòng tiếp theo
  }
  free(smsCopy); // Giải phóng bộ nhớ đã cấp phát

  // Lưu vào EEPROM nếu có thay đổi
  if (dataChanged) {
    if (saveContactsToEEPROM()) {
      Serial.println("Da luu thay doi vao EEPROM!");
      beep_led(3, 150);
      sendConfirmationSMS(sender); // Gửi tin nhắn xác nhận
      return true;
    } else {
      Serial.println("Loi: Khong luu duoc thay doi vao EEPROM!");
      return false; // Trả về false nếu lưu lỗi
    }
  } else {
      Serial.println("Khong co thay doi trong danh ba tu SMS nay.");
      // Không cần gửi SMS xác nhận nếu không có gì thay đổi
      return true; // Vẫn coi là thành công vì đã xử lý xong SMS
  }
}

/**
 * @brief Gửi SMS xác nhận cấu hình thành công.
 */
void sendConfirmationSMS(const char* recipient) {
  if (!recipient || strlen(recipient) < 9) {
      Serial.println("Loi: So dien thoai nguoi gui khong hop le de gui xac nhan.");
      return;
  }
  Serial.printf("Dang gui SMS xac nhan den: %s\n", recipient);
  char cmd[50];
  sprintf(cmd, "AT+CMGS=\"%s\"", recipient);

  while(simSerial.available()) simSerial.read();
  simSerial.println(cmd);
  String response = readSerial(1000); // Chờ dấu nhắc ">"
  if (response.indexOf('>') != -1) {
      simSerial.print("VDetect: Da cap nhat danh ba lien he thanh cong.");
      delay(100);
      simSerial.write(0x1A); // Ctrl+Z
      response = readSerial(10000); // Chờ phản hồi gửi
      Serial.print("Phan hoi gui SMS Xac nhan: "); Serial.println(response);
      if (response.indexOf("OK") != -1 || response.indexOf("+CMGS:") != -1) {
         Serial.println("Gui SMS xac nhan thanh cong.");
      } else {
         Serial.println("Loi: Gui SMS xac nhan that bai (sau Ctrl+Z).");
      }
  } else {
       Serial.println("Loi: Khong nhan duoc '>' de gui noi dung SMS xac nhan.");
  }
}

/**
 * @brief Đọc và cập nhật dữ liệu GPS.
 */
void updateGPS() {
  bool newDataProcessed = false;
  unsigned long startRead = millis();
  while (gpsSerial.available() && (millis() - startRead < 75)) { // Đọc tối đa 75ms mỗi lần gọi
    if (gps.encode(gpsSerial.read())) {
      newDataProcessed = true;
    }
  }

  if (newDataProcessed) {
    if (gps.location.isValid() && gps.location.isUpdated()) {
      currentLat = gps.location.lat();
      currentLng = gps.location.lng();
      // Chỉ đặt gpsFix = true nếu độ chính xác (HDOP) đủ tốt (ví dụ < 3.0)
      if (gps.hdop.isValid() && gps.hdop.value() < 500) { // hdop value is * 100
          gpsFix = true;
          lastGpsRead = millis();
          // In log tọa độ chỉ khi thay đổi đáng kể để đỡ spam
          static float prevLat = 0.0, prevLng = 0.0;
          if (abs(currentLat - prevLat) > 0.00001 || abs(currentLng - prevLng) > 0.00001) {
            //  Serial.printf("GPS Update: Lat=%.6f, Lng=%.6f, Sats=%d, HDOP=%.2f\n", currentLat, currentLng, gps.satellites.value(), gps.hdop.hdop());
             prevLat = currentLat;
             prevLng = currentLng;
          }
      } else {
         // Vị trí valid nhưng độ chính xác không đủ tốt
         if(gpsFix) { // Nếu trước đó có fix tốt
              Serial.println("GPS HDOP too high, ignoring location.");
              // Giữ fix cũ trong thời gian ngắn? Hay đặt là false? Tạm thời đặt false.
              gpsFix = false;
         }
      }
    } else { // Dữ liệu GPS không valid
      if (gpsFix && (millis() - lastGpsRead > 10000)) {
         gpsFix = false; currentLat = 0.0; currentLng = 0.0;
         Serial.println("!!! GPS Fix Lost (Invalid Data) !!!");
      }
    }
  } else { // Không có dữ liệu mới từ GPS
      if (gpsFix && (millis() - lastGpsRead > 30000)) {
            gpsFix = false; currentLat = 0.0; currentLng = 0.0;
            Serial.println("!!! GPS Data Timeout - Fix Lost !!!");
      }
  }
  // In trạng thái chờ fix nếu cần debug
  static unsigned long lastWaitMsg = 0;
  if (!gpsFix && (millis() - lastWaitMsg > 5000)) { // In 5 giây một lần
      // Serial.println("Waiting for valid GPS fix (HDOP < 3.0)...");
      lastWaitMsg = millis();
  }
}

/**
 * @brief Gửi SMS chứa vị trí GPS hiện tại.
 */
void sendLocationSMS(const char* recipient) {
    char smsContent[150];

    Serial.printf("Nhan duoc yeu cau vi tri tu: %s\n", recipient);

    if (gpsFix) {
        // Thêm timestamp vào link nếu muốn
        // char timestamp[25];
        // sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02dZ",
        //         gps.date.year(), gps.date.month(), gps.date.day(),
        //         gps.time.hour(), gps.time.minute(), gps.time.second());
        sprintf(smsContent, "Vi tri VDetect: https://maps.google.com/maps?q=%.6f,%.6f", currentLat, currentLng);
        Serial.printf(" >> Co vi tri GPS hop le. Dang tao link: %s\n", smsContent);
    } else {
        strcpy(smsContent, "VDetect: Khong the xac dinh vi tri GPS chinh xac luc nay.");
        Serial.println(" >> Khong co vi tri GPS hop le de gui.");
    }

    // Gửi SMS đi
    Serial.printf("Dang gui SMS vi tri den: %s\n", recipient);
    char cmd[50];
    sprintf(cmd, "AT+CMGS=\"%s\"", recipient);

    while(simSerial.available()) simSerial.read();
    simSerial.println(cmd);
    String response = readSerial(1000);
    if (response.indexOf('>') != -1) {
        simSerial.print(smsContent);
        delay(100);
        simSerial.write(0x1A); // Ctrl+Z
        response = readSerial(10000);
        Serial.print("Phan hoi gui SMS Vi tri: "); Serial.println(response);
        if (response.indexOf("OK") != -1 || response.indexOf("+CMGS:") != -1) {
            Serial.println("Gui SMS vi tri thanh cong.");
        } else {
            Serial.println("Loi: Gui SMS vi tri that bai (sau Ctrl+Z).");
        }
    } else {
        Serial.println("Loi: Khong nhan duoc '>' de gui noi dung SMS vi tri.");
    }
}


/**
 * @brief Kiểm tra và xử lý dữ liệu đến từ SIM (URC, SMS). *Đã sửa đọc CMGR và thêm xử lý "Vị trí"*
 */
// --- Hàm mới: So sánh SĐT bỏ qua tiền tố ---
// Trả về true nếu 9 số cuối giống nhau
bool comparePhoneNumbers(const char* num1, const char* num2) {
    int len1 = strlen(num1);
    int len2 = strlen(num2);
    // So sánh 9 chữ số cuối cùng
    if (len1 >= 9 && len2 >= 9) {
        return (strcmp(num1 + len1 - 9, num2 + len2 - 9) == 0);
    }
    return false; // Không đủ 9 số thì coi như không khớp
}


// --- Hàm checkAndProcessSIM - Sửa lại phần xử lý SMS ---
/**

 * @brief Kiểm tra và xử lý dữ liệu đến từ SIM (URC, SMS, Call). *Đã sửa lỗi timing RING/CLIP*
 */
/**
 * @brief Kiểm tra và xử lý dữ liệu đến từ SIM (URC, SMS, Call). *Đã thêm gửi SMS lỗi*
 */
void checkAndProcessSIM() {
  // --- Xử lý dữ liệu đến từ SIM ---
  while (simSerial.available()) {
      String response = simSerial.readStringUntil('\n');
      response.trim();
      if (response.length() > 0) {
          Serial.print("Nhan URC/Data: "); Serial.println(response);

          // --- Ưu tiên xử lý Cuộc gọi ---
          if (response.indexOf("RING") != -1) { /* ... code xử lý RING và CLIP như phiên bản trước ... */
              if (!isRinging) { Serial.println(">>> CUOC GOI DEN <<<"); isRinging = true;
                   if (incomingCallerNumber.length() > 0 && !findMeAlarmActive && phone1[0] != '\0') {
                       if (comparePhoneNumbers(incomingCallerNumber.c_str(), phone1)) { Serial.println("   -> (RING Check, CLIP received first) Cuoc goi tu phone1. Kich hoat Tim Xe!"); findMeAlarmActive = true; }
                       else { Serial.println("   -> (RING Check, CLIP received first) Cuoc goi tu so khac."); findMeAlarmActive = false; }
                   } else if (!findMeAlarmActive) { Serial.println("   -> RING den truoc CLIP, cho CLIP de xac dinh nguoi goi..."); }
               } continue;
          }
          if (response.indexOf("+CLIP:") != -1) { /* ... code xử lý CLIP như phiên bản trước ... */
               int firstQuote = response.indexOf('"'); if (firstQuote != -1) { int secondQuote = response.indexOf('"', firstQuote + 1); if (secondQuote != -1) {
                   incomingCallerNumber = response.substring(firstQuote + 1, secondQuote); Serial.printf(" >> So goi den (CLIP): %s\n", incomingCallerNumber.c_str());
                   if (isRinging && !findMeAlarmActive && phone1[0] != '\0') {
                       if (comparePhoneNumbers(incomingCallerNumber.c_str(), phone1)) { Serial.println("   -> (CLIP Check while Ringing) Cuoc goi tu phone1. Kich hoat Tim Xe!"); findMeAlarmActive = true; }
                       else { Serial.println("   -> (CLIP Check while Ringing) Cuoc goi tu so khac."); } } } } continue;
          }
          if (isRinging && (response.indexOf("NO CARRIER") != -1 || response.indexOf("BUSY") != -1 || response.indexOf("NO ANSWER") != -1 || response.indexOf("VOICE CALL: END") != -1 )) {
               Serial.println(">>> CUOC GOI KET THUC <<<"); isRinging = false; findMeAlarmActive = false; incomingCallerNumber = ""; continue;
          }
          if(isRinging || findMeAlarmActive) continue; // Bỏ qua xử lý SMS nếu đang gọi hoặc đang tìm xe

          // --- Xử lý SMS mới (+CMTI) ---
          int cmtIndex = response.indexOf("+CMTI:");
          if (cmtIndex != -1) {
              int smsIndex = -1; /* ... code parse smsIndex ... */
              int commaPos = response.indexOf(','); int quotePos = response.indexOf('"', cmtIndex);
              if (commaPos > quotePos && quotePos != -1) { smsIndex = response.substring(commaPos + 1).toInt(); }
              else { const char *p = response.c_str() + cmtIndex + strlen("+CMTI:"); while (*p && (*p != ',')) p++; if (*p == ',') smsIndex = atoi(p + 1); }

              if (smsIndex > 0) {
                  Serial.printf("Co tin nhan moi (index: %d)\n", smsIndex);
                  beep_led(2, 100);

                  // --- Đọc SMS ---
                  char cmd[20]; sprintf(cmd, "AT+CMGR=%d", smsIndex);
                  // ... (Code đọc SMS nhiều dòng vào smsBody và senderNumLocal như cũ) ...
                   Serial.printf("Gui lenh doc SMS: %s\n", cmd); while(simSerial.available()) simSerial.read(); simSerial.println(cmd);
                   String smsFullResponse = ""; String smsBody = ""; String senderNumLocal = "";
                   bool headerFound = false; bool bodyStarted = false; unsigned long readStart = millis(); bool okReceived = false;
                   Serial.println("--- Bat dau doc phan hoi CMGR ---");
                   while (millis() - readStart < 5000 && !okReceived) { /* ... code đọc ... */
                        if (simSerial.available()) { String line = simSerial.readStringUntil('\n'); line.trim(); if (line.length() > 0) { smsFullResponse += line + "\n"; if (line.startsWith("+CMGR:")) { headerFound = true; bodyStarted = false; int firstComma = line.indexOf(','); int secondComma = line.indexOf(',', firstComma + 1); if (firstComma != -1 && secondComma != -1) { int start = line.indexOf('"', firstComma) + 1; int end = line.indexOf('"', start); if (start != 0 && end != -1 && (end - start) < PHONE_NUMBER_SIZE) { senderNumLocal = line.substring(start, end); } } if(senderNumLocal.length() > 0) Serial.printf("   Nguoi gui (tu CMGR): %s\n", senderNumLocal.c_str()); } else if (headerFound && line.equals("OK")) { okReceived = true; bodyStarted = false; } else if (headerFound) { smsBody += line + "\n"; bodyStarted = true; } else if (line.equals("OK")) { okReceived = true; } else if (line.indexOf("ERROR") != -1) { okReceived = true; } } readStart = millis(); } delay(10);
                   }
                   Serial.println("--- Ket thuc doc phan hoi CMGR ---");
                   if (smsBody.endsWith("\n")) smsBody.remove(smsBody.length() - 1);
                   if (!okReceived) Serial.println("Loi: Timeout/Loi khi doc SMS tu module.");
                  // --- Kết thúc đọc SMS ---


                  // --- Xử lý nội dung SMS đã đọc ---
                  if (smsBody.length() > 0 || (okReceived && headerFound && smsBody.length() == 0) ) {
                       if (smsBody.length() > 0) { Serial.println("--- Noi dung SMS Body Trich Xuat ---\n" + smsBody + "\n----------------------------------"); }
                       else { Serial.println("--- Noi dung SMS rong ---"); }

                      // *** SỬA LOGIC XỬ LÝ SMS VÀ THÊM BÁO LỖI ***
                      bool processed = false; // Cờ đánh dấu SMS đã được xử lý hay chưa

                      // 1. Ưu tiên kiểm tra lệnh "Vi tri" (không dấu)
                      if (smsBody.equalsIgnoreCase("Vi tri")) {
                          processed = true; // Đánh dấu đã xử lý (hoặc cố gắng xử lý) lệnh này

                          // Kiểm tra người gửi có phải phone1 không
                          if (senderNumLocal.length() > 0 && phone1[0] != '\0' && comparePhoneNumbers(senderNumLocal.c_str(), phone1)) {
                              // ĐÚNG: Lệnh đúng, người gửi đúng
                              Serial.println(" >> Nhan yeu cau 'Vi tri' tu chu nhan (phone1).");
                              sendLocationSMS(phone1); // Gọi hàm gửi vị trí
                          }
                          // *** THÊM: Xử lý lỗi sai người gửi ***
                          else if (senderNumLocal.length() > 0) {
                              // SAI NGƯỜI GỬI: Lệnh đúng nhưng người gửi không phải phone1
                              Serial.println(" >> LOI: Nhan yeu cau 'Vi tri' nhung KHONG PHAI tu phone1.");
                              // Gửi SMS báo lỗi về cho người gửi sai
                              sendErrorSMS(senderNumLocal.c_str(), "Lenh 'Vi tri' chi chap nhan tu so Phone1 da cau hinh.");
                          }
                          // *** KẾT THÚC THÊM ***
                          else {
                               // Lệnh đúng nhưng không xác định được người gửi?
                               Serial.println(" >> Nhan yeu cau 'Vi tri' nhung khong xac dinh duoc nguoi gui.");
                          }
                      }

                      // 2. Nếu không phải lệnh "Vi tri", thử xử lý như SMS cấu hình
                      if (!processed && senderNumLocal.length() > 0) {
                          processed = true;
                          Serial.println(" >> Xu ly nhu SMS cau hinh.");
                          bool parseResult = parseSMSContent(smsBody.c_str(), senderNumLocal.c_str());
                          if (!parseResult) {
                               // Giả sử lỗi duy nhất là EEPROM
                               Serial.println(" >> LOI: Luu cau hinh vao EEPROM that bai.");
                               sendErrorSMS(senderNumLocal.c_str(), "Loi luu cau hinh vao bo nho.");
                          }
                      }

                      // 3. Các trường hợp SMS không được xử lý khác
                      if (!processed) {
                           Serial.println(" >> SMS khong duoc xu ly (lenh khong hop le hoac khong co nguoi gui).");
                           // Tùy chọn: Gửi lỗi về người gửi nếu xác định được senderNumLocal
                           // if (senderNumLocal.length() > 0) {
                           //    sendErrorSMS(senderNumLocal.c_str(), "Lenh hoac noi dung tin nhan khong hop le.");
                           // }
                      }
                      // *** KẾT THÚC SỬA LOGIC ***

                  } else { Serial.println("Khong trich xuat duoc noi dung SMS (do loi doc)."); }
                  // ------------------------------------


                  // Xóa SMS sau khi xử lý
                  Serial.printf("Gui lenh xoa SMS index %d\n", smsIndex);
                  sprintf(cmd, "AT+CMGD=%d", smsIndex);
                  if (sendCommand(cmd, 5000, true)) { Serial.printf("Da xoa SMS index %d.\n", smsIndex); }
                  else { Serial.printf("Loi/Khong xoa duoc SMS index %d.\n", smsIndex); }

              } else { Serial.println("Khong the trich xuat index SMS tu +CMTI."); }
          } // Kết thúc xử lý +CMTI
      } // Kết thúc if (response.length() > 0)
  } // Kết thúc while (simSerial.available())

  // Phần kiểm tra isRinging cuối hàm để tắt còi/LED nếu cần
  // Sẽ được handleFindMeAlarm xử lý nên có thể bỏ qua ở đây
}
/**
 * @brief Quản lý trạng thái báo động Tìm Xe (còi + LED) và kiểm tra nút dừng.
 */
/**
 * @brief Quản lý trạng thái báo động Tìm Xe (còi + LED) và kiểm tra nút dừng. *Đã sửa lỗi*
 */
void handleFindMeAlarm() {
  // --- Xử lý Nút Dừng Tìm Xe (GPIO 4) ---
  // Dùng biến static để chỉ xử lý 1 lần khi nút được nhấn xuống lần đầu
  static bool findMeButtonPressedState = false; // Lưu trạng thái nút đã được xử lý hay chưa
  bool isFindMeButtonCurrentlyPressed = (digitalRead(FIND_ME_BUTTON_PIN) == LOW);

  if (findMeAlarmActive) { // Chỉ xử lý nút dừng khi báo động đang bật
    if (isFindMeButtonCurrentlyPressed && !findMeButtonPressedState) {
      // Nút vừa được nhấn xuống (trước đó chưa nhấn)
      Serial.println("Nut dung Tim Xe duoc nhan -> Tat bao dong.");
      findMeAlarmActive = false;  // Tắt cờ báo động
      findMeButtonPressedState = true; // Đánh dấu đã xử lý việc nhấn nút

      // --- ĐÃ XÓA CÁC DÒNG GÂY LỖI TRUY CẬP BIẾN KHÔNG TỒN TẠI ---
      // buttonPressStartTime = 0; // <<< ĐÃ XÓA
      // settingReference = false;   // <<< ĐÃ XÓA

      // Tùy chọn: Gửi lệnh ATH để kết thúc cuộc gọi từ phone1 nếu muốn
      // Serial.println("Gui lenh ATH de ngat cuoc goi...");
      // sendCommand("ATH", 2000, false);

    } else if (!isFindMeButtonCurrentlyPressed) {
      // Nếu nút được nhả ra, reset trạng thái đã xử lý nút
      findMeButtonPressedState = false;
    }
  } else {
      // Nếu báo động không bật, luôn reset trạng thái nút dừng
       findMeButtonPressedState = false;
  }


  // --- Điều khiển Còi và LED dựa trên trạng thái findMeAlarmActive ---
  if (findMeAlarmActive) {
    // Tạo hiệu ứng nhấp nháy liên tục
    unsigned long interval = 500; // Chu kỳ nháy (ms) - Tăng/giảm để đổi tốc độ
    unsigned long onDuration = 250; // Thời gian kêu/sáng trong 1 chu kỳ (ms)
    if ((millis() % interval) < onDuration) {
      digitalWrite(BUZZER_PIN, HIGH);
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(LED_PIN, LOW);
    }
  } else {
    // Nếu không có báo động Tìm Xe, đảm bảo còi và LED tắt
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
  }
}
/**
 * @brief Gửi SMS thông báo lỗi đến người nhận chỉ định.
 */
void sendErrorSMS(const char* recipient, const char* errorMessage) {
    if (!recipient || strlen(recipient) < 9) { // Kiểm tra SĐT cơ bản
        Serial.println("Loi: So dien thoai nguoi nhan khong hop le de gui SMS.");
        return;
    }
    // Giới hạn độ dài errorMessage để tránh tràn buffer SMS (khoảng < 140 ký tự ASCII)
    char fullErrorMessage[160];
    snprintf(fullErrorMessage, sizeof(fullErrorMessage), "VDetect Loi: %s", errorMessage); // Thêm tiền tố lỗi

    Serial.printf("Dang gui SMS loi den: %s - Noi dung: %s\n", recipient, fullErrorMessage);
    char cmd[50];
    sprintf(cmd, "AT+CMGS=\"%s\"", recipient);

    while(simSerial.available()) simSerial.read(); // Xóa buffer nhận
    simSerial.println(cmd);
    String response = readSerial(1000); // Chờ dấu nhắc ">"
    if (response.indexOf('>') != -1) {
        simSerial.print(fullErrorMessage); // Gửi nội dung lỗi
        delay(100);
        simSerial.write(0x1A); // Gửi Ctrl+Z
        response = readSerial(10000); // Chờ phản hồi gửi
        Serial.print("Phan hoi gui SMS Loi: "); Serial.println(response);
        if (response.indexOf("OK") != -1 || response.indexOf("+CMGS:") != -1) {
            Serial.println("Gui SMS loi thanh cong.");
        } else {
            Serial.println("Loi: Gui SMS loi that bai (sau Ctrl+Z).");
        }
    } else {
        Serial.println("Loi: Khong nhan duoc '>' de gui noi dung SMS loi.");
    }
}
