// notifications.cpp
#include "notifications.h"

// --- ĐỊNH NGHĨA CÁC BIẾN TOÀN CỤC ĐÃ KHAI BÁO EXTERN TRONG .h ---
HardwareSerial simSerial_Notif(2);
HardwareSerial gpsSerial_Notif(1);
TinyGPSPlus gps_Notif;

char userName_Notif[USERNAME_SIZE_NOTIF] = "Chu Xe";
char phone1_Notif[PHONE_NUMBER_SIZE_NOTIF] = "";
char phone2_Notif[PHONE_NUMBER_SIZE_NOTIF] = "";
char phone3_Notif[PHONE_NUMBER_SIZE_NOTIF] = "";
char zalo1_Notif[PHONE_NUMBER_SIZE_NOTIF] = "";
char zalo2_Notif[PHONE_NUMBER_SIZE_NOTIF] = "";
char zalo3_Notif[PHONE_NUMBER_SIZE_NOTIF] = "";

// --- Trạng thái ---
bool isRinging_Notif = false; // Trạng thái cuộc gọi

// --- Dữ liệu GPS ---
float currentLat_Notif = 0.0;        // Vĩ độ
float currentLng_Notif = 0.0;        // Kinh độ
bool gpsFix_Notif = false;           // Trạng thái có tín hiệu GPS hợp lệ
unsigned long lastGpsRead_Notif = 0; // Thời điểm đọc GPS hợp lệ cuối cùng

// --- Buffers ---
char smsBuffer_Notif[512]; // Buffer đọc nội dung SMS (cho parseSMSContent)

bool findMeAlarmActive_Notif = false; // Cờ báo hiệu trạng thái báo động Tìm Xe đang BẬT
String incomingCallerNumber_Notif = "";

// ... (khai báo và khởi tạo các biến phone, zalo, currentLat/Lng, gpsFix, isRinging, findMeAlarmActive, incomingCallerNumber khác tương tự)

// --- ĐỊNH NGHĨA CÁC HÀM ---
void beep_led_notifications(int count, int duration_ms, int ledPin, int buzzerPin)
{
  for (int i = 0; i < count; i++)
  {
    digitalWrite(ledPin, HIGH);
    digitalWrite(buzzerPin, HIGH);
    delay(duration_ms);
    digitalWrite(ledPin, LOW);
    digitalWrite(buzzerPin, LOW);
    if (count > 1 && i < count - 1)
    {
      delay(duration_ms);
    }
  }
}

void initNotifications()
{
  pinMode(SIM_ENABLE_PIN_NOTIF, OUTPUT);
  digitalWrite(SIM_ENABLE_PIN_NOTIF, LOW);
  Serial.println("Da kich hoat chan Enable SIM (notifications.cpp).");

  Serial.println("Khoi tao Serial2 cho SIM (notifications.cpp)...");
  simSerial_Notif.begin(SIM_BAUD_RATE_NOTIF, SERIAL_8N1, SIM_SERIAL_RX_PIN_NOTIF, SIM_SERIAL_TX_PIN_NOTIF);

  Serial.println("Khoi tao Serial1 cho GPS (notifications.cpp)...");
  gpsSerial_Notif.begin(GPS_BAUD_RATE_NOTIF, SERIAL_8N1, GPS_SERIAL_RX_PIN_NOTIF, GPS_SERIAL_TX_PIN_NOTIF);

  if (!EEPROM.begin(EEPROM_SIZE_NOTIF))
  {
    Serial.println("!!! LOI KHOI TAO EEPROM (notifications.cpp) !!!");
  }
  else
  {
    Serial.println("EEPROM khoi tao thanh cong (notifications.cpp).");
    loadContactsFromEEPROM_notifications();
  }

  Serial.println("Cho module SIM khoi dong (5 giay - notifications.cpp)...");
  delay(5000);

  if (initializeSIM_notifications())
  {
    // ... (các lệnh cấu hình SIM như AT+CMGF, AT+CNMI, AT+CLIP, AT+CMGD) ...
    // Nhớ dùng sendCommand_notifications và Serial

    Serial.println("****************************");
    Serial.println("* MODULE SIM DA SAN SANG! *");
    Serial.println("****************************");
    beep_led_notifications(1, 200);

    // --- Cấu hình SIM sau khi khởi tạo thành công ---
    sendCommand_notifications("AT+CMGF=1", 2000);
    delay(100); // Đặt chế độ SMS Text
    Serial.println("Dat che do thong bao SMS (CNMI=2,1)...");
    sendCommand_notifications("AT+CNMI=2,1,0,0,0", 2000);
    delay(100); // Yêu cầu +CMTI
    Serial.println("Kiem tra lai CNMI vua dat:");
    sendCommand_notifications("AT+CNMI?", 3000, false);
    delay(100);
    sendCommand_notifications("AT+CLIP=1", 2000);
    delay(100); // Bật Caller ID
    Serial.println("Dang xoa tin nhan cu tren SIM (neu co)...");
    sendCommand_notifications("AT+CMGD=1,4", 5000, true);
    delay(1000); // Xóa SMS cũ
    Serial.println("VDetect san sang nhan SMS cau hinh & Cuoc goi.");
  }
  else
  {
    Serial.println("!!! LOI: KHONG KHOI TAO DUOC MODULE SIM (notifications.cpp) !!!");
  }
}

// --- BẠN CẦN COPY CÁC HÀM initializeSIM, sendCommand, readSerial, các hàm EEPROM, ---
// --- các hàm SMS, updateGPS, checkAndProcessSIM TỪ FILE sms.cpp CỦA BẠN VÀO ĐÂY ---
// --- SAU ĐÓ SỬA ĐỔI CHÚNG:                                                        ---
// --- 1. Đổi tên hàm (thêm _notifications).                                       ---
// --- 2. Thay thế Serial.print bằng Serial.print.                           ---
// --- 3. Đảm bảo chúng sử dụng các biến toàn cục có hậu tố _Notif (ví dụ phone1_Notif) ---
// --- 4. Gọi các hàm con cũng phải có hậu tố _notifications.                      ---

// Ví dụ:
// String readSerial_notifications(unsigned long timeout, HardwareSerial& commSerial) {
//   String response = ""; /* ... */ return response;
// }
/**
 * @brief Đọc dữ liệu từ Serial của SIM với timeout, loại bỏ ký tự thừa.
 */
String readSerial_notifications(unsigned long timeout)
{
  String response = "";
  unsigned long startTime = millis();
  while (millis() - startTime < timeout)
  {
    while (simSerial_Notif.available())
    {
      char c = simSerial_Notif.read();
      if (c != '\r')
      { // Bỏ qua ký tự \r
        response += c;
      }
      startTime = millis(); // Reset timeout khi nhận được ký tự
    }
    if (response.length() > 0 && !simSerial_Notif.available())
    {
      delay(50); // Chờ thêm 1 chút xíu xem còn gì nữa không
      if (!simSerial_Notif.available())
        break;
    }
    delay(1); // Delay nhỏ tránh busy-waiting
  }
  response.trim(); // Xóa khoảng trắng, \n thừa ở đầu/cuối
  return response;
}
// String sendCommand_notifications(const char* cmd, unsigned long timeout, bool requireOK, bool printResponse) {
//    /* ... sử dụng simSerial_Notif và readSerial_notifications ... */ return response;
// }
String sendCommand_notifications(const char *cmd, unsigned long timeout, bool requireOK, bool printResponse)
{
  if (printResponse)
  {
    Serial.printf("Gui lenh: %s\n", cmd);
  }
  while (simSerial_Notif.available())
    simSerial_Notif.read(); // Xóa buffer nhận trước khi gửi
  simSerial_Notif.println(cmd);

  String response = readSerial_notifications(timeout); // Đọc phản hồi

  if (printResponse)
  {
    Serial.print("Phan hoi: ");
    Serial.println(response);
  }

  if (response.length() == 0)
  {
    if (printResponse)
      Serial.println(" >> Timeout! Khong co phan hoi.");
    return ""; // Trả về chuỗi rỗng nếu timeout
  }

  // Kiểm tra các lỗi phổ biến trước khi kiểm tra OK
  if (response.indexOf("ERROR") != -1 || response.indexOf("BUSY") != -1 || response.indexOf("NO CARRIER") != -1)
  {
    if (printResponse)
      Serial.println(" >> Phan hoi chua ERROR/BUSY/NO CARRIER.");
    // Nếu requireOK là true, thì lỗi này cũng coi như thất bại
    // Nếu requireOK là false, vẫn trả về response để hàm gọi tự xử lý lỗi
    return requireOK ? "" : response;
  }

  if (requireOK)
  {
    if (response.indexOf("OK") != -1)
    {
      return response; // Có "OK", trả về phản hồi
    }
    else
    {
      if (printResponse)
        Serial.println(" >> Loi: Khong tim thay 'OK' trong phan hoi.");
      return ""; // Không có "OK", trả về chuỗi rỗng
    }
  }
  else
  {
    // Nếu không yêu cầu OK, chỉ cần có phản hồi là trả về
    return response;
  }
}

// bool initializeSIM_notifications() { /* ... sử dụng sendCommand_notifications ... */ }
bool initializeSIM_notifications()
{
  // 1. AT
  Serial.println("1. Kiem tra giao tiep co ban (AT)...");
  if (sendCommand_notifications("AT", 5000, false).length() == 0)
  {
    Serial.println(" >> Buoc 1 THAT BAI: Module khong phan hoi lenh AT.");
    return false;
  }
  Serial.println(" >> Buoc 1 OK: Module co phan hoi.");
  delay(100);

  // 2. ATE0
  Serial.println("2. Tat echo (ATE0)...");
  if (sendCommand_notifications("ATE0", 2000, true).length() == 0)
  {
    Serial.println(" >> Buoc 2 THAT BAI: Khong tat duoc echo.");
    // return false; // Có thể bỏ qua lỗi này
  }
  else
  {
    Serial.println(" >> Buoc 2 OK: Da tat echo.");
  }
  delay(100);

  // 4. AT+CPIN?
  Serial.println("4. Kiem tra trang thai SIM Card (AT+CPIN?)...");
  unsigned long startTime = millis();
  bool pinReady = false;
  while (millis() - startTime < SIM_PIN_TIMEOUT_NOTIF)
  {
    String response = sendCommand_notifications("AT+CPIN?", 2000, false, false); // Tắt log của sendCommand ở đây
    if (response.length() > 0)
    {
      Serial.print("   Phan hoi CPIN: ");
      Serial.println(response); // In log kiểm tra
      if (response.indexOf("READY") != -1)
      {
        pinReady = true;
        Serial.println(" >> Buoc 4 OK: Trang thai SIM la READY!");
        break;
      }
      else if (response.indexOf("SIM PIN") != -1)
      {
        Serial.println(" >> LOI: SIM yeu cau ma PIN!");
        return false;
      }
      else if (response.indexOf("SIM PUK") != -1)
      {
        Serial.println(" >> LOI: SIM yeu cau ma PUK!");
        return false;
      }
    }
    else
    {
      Serial.println("   Khong nhan duoc phan hoi AT+CPIN?");
    }
    delay(1500);
  }
  if (!pinReady)
  {
    Serial.println(" >> Buoc 4 THAT BAI: SIM khong san sang (Timeout).");
    return false;
  }
  delay(100);

  // 5. AT+CREG?
  Serial.println("5. Kiem tra dang ky mang (AT+CREG?)...");
  startTime = millis();
  bool registered = false;
  int retries = 0;
  while (millis() - startTime < SIM_INIT_TIMEOUT_NOTIF)
  {
    String response = sendCommand_notifications("AT+CREG?", 2000, false, false); // Tắt log của sendCommand
    if (response.length() > 0)
    {
      Serial.print("   Phan hoi CREG: ");
      Serial.println(response); // In log kiểm tra
      int firstComma = response.indexOf(',');
      if (firstComma != -1)
      {
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

        if (stat == 1 || stat == 5)
        { // 1 = Registered home, 5 = Registered roaming
          registered = true;
          Serial.print(" >> Buoc 5 OK: Da dang ky mang (Trang thai: ");
          Serial.print(stat);
          Serial.println(")");
          break;
        }
        else
        {
          Serial.print(" >> Chua dang ky mang (Trang thai: ");
          Serial.print(stat);
          Serial.println("), dang cho...");
        }
      }
      else
      {
        // Phản hồi CREG không đúng định dạng mong đợi?
        Serial.println("   Phan hoi CREG khong chua dau phay ','?");
      }
    }
    else
    {
      Serial.println("   Khong nhan duoc phan hoi AT+CREG?");
    }
    delay((retries < 5) ? 2500 : 5000); // Chờ lâu hơn nếu thử nhiều lần
    retries++;
  }
  if (!registered)
  {
    Serial.println(" >> Buoc 5 THAT BAI: Khong dang ky duoc mang (Timeout).");
    Serial.println("Kiem tra chat luong song (AT+CSQ):");
    sendCommand_notifications("AT+CSQ", 3000, false); // Kiểm tra sóng khi lỗi
    return false;
  }
  return true;
}

// bool comparePhoneNumbers_notifications(const char* num1, const char* num2) { /* ... code từ sms.cpp ... */ }
/**
 * @brief Kiểm tra và xử lý dữ liệu đến từ SIM (URC, SMS). *Đã sửa đọc CMGR và thêm xử lý "Vị trí"*
 */
// Trả về true nếu 9 số cuối giống nhau
bool comparePhoneNumbers_notifications(const char *num1, const char *num2)
{
  int len1 = strlen(num1);
  int len2 = strlen(num2);
  // So sánh 9 chữ số cuối cùng
  if (len1 >= 9 && len2 >= 9)
  {
    return (strcmp(num1 + len1 - 9, num2 + len2 - 9) == 0);
  }
  return false; // Không đủ 9 số thì coi như không khớp
}
// void loadContactsFromEEPROM_notifications() { /* ... code từ sms.cpp, sửa Serial.print ... */ }
void loadContactsFromEEPROM_notifications()
{
  Serial.println("Doc du lieu tu EEPROM...");
  if (!EEPROM.begin(EEPROM_SIZE_NOTIF))
  {
    Serial.println("Loi EEPROM.begin() khi doc!");
    return; // Không thể tiếp tục nếu EEPROM lỗi
  }
  if (EEPROM.read(EEPROM_MAGIC_ADDR_NOTIF) == EEPROM_MAGIC_VALUE_NOTIF)
  {
    int currentAddr = EEPROM_DATA_ADDR_NOTIF;
    EEPROM.get(currentAddr, userName_Notif);
    currentAddr += USERNAME_SIZE_NOTIF;
    EEPROM.get(currentAddr, phone1_Notif);
    currentAddr += PHONE_NUMBER_SIZE_NOTIF;
    EEPROM.get(currentAddr, phone2_Notif);
    currentAddr += PHONE_NUMBER_SIZE_NOTIF;
    EEPROM.get(currentAddr, phone3_Notif);
    currentAddr += PHONE_NUMBER_SIZE_NOTIF;
    EEPROM.get(currentAddr, zalo1_Notif);
    currentAddr += PHONE_NUMBER_SIZE_NOTIF;
    EEPROM.get(currentAddr, zalo2_Notif);
    currentAddr += PHONE_NUMBER_SIZE_NOTIF;
    EEPROM.get(currentAddr, zalo3_Notif);
    currentAddr += PHONE_NUMBER_SIZE_NOTIF;
    Serial.println("Da tai danh ba tu EEPROM.");
  }
  else
  {
    Serial.println("EEPROM trong hoac du lieu khong hop le. Danh ba se rong.");

    strcpy(phone1_Notif, "");
    strcpy(phone2_Notif, "");
    strcpy(phone3_Notif, "");
    strcpy(zalo1_Notif, "");
    strcpy(zalo2_Notif, "");
    strcpy(zalo3_Notif, "");
  }
  // EEPROM.end(); // Không cần end nếu dùng liên tục? Tùy core ESP32.
}
// bool saveContactsToEEPROM_notifications() { /* ... code từ sms.cpp, sửa Serial.print ... */ }
bool saveContactsToEEPROM_notifications()
{
  Serial.println("Luu danh ba vao EEPROM...");
  if (!EEPROM.begin(EEPROM_SIZE_NOTIF))
  { // Cần begin trước khi ghi
    Serial.println("Loi EEPROM.begin() khi luu!");
    return false;
  }
  int currentAddr = EEPROM_DATA_ADDR_NOTIF;
  EEPROM.put(currentAddr, userName_Notif);
  currentAddr += USERNAME_SIZE_NOTIF;
  EEPROM.put(currentAddr, phone1_Notif);
  currentAddr += PHONE_NUMBER_SIZE_NOTIF;
  EEPROM.put(currentAddr, phone2_Notif);
  currentAddr += PHONE_NUMBER_SIZE_NOTIF;
  EEPROM.put(currentAddr, phone3_Notif);
  currentAddr += PHONE_NUMBER_SIZE_NOTIF;
  EEPROM.put(currentAddr, zalo1_Notif);
  currentAddr += PHONE_NUMBER_SIZE_NOTIF;
  EEPROM.put(currentAddr, zalo2_Notif);
  currentAddr += PHONE_NUMBER_SIZE_NOTIF;
  EEPROM.put(currentAddr, zalo3_Notif);
  currentAddr += PHONE_NUMBER_SIZE_NOTIF;
  EEPROM.write(EEPROM_MAGIC_ADDR_NOTIF, EEPROM_MAGIC_VALUE_NOTIF);

  if (EEPROM.commit())
  {
    Serial.println("Luu EEPROM thanh cong.");
    // EEPROM.end(); // Commit xong có thể end
    return true;
  }
  else
  {
    Serial.println("Loi: Luu EEPROM that bai!");
    // EEPROM.end();
    return false;
  }
}
// bool parseSMSContent_notifications(const char* smsBody, const char* senderNum) { /* ... code từ sms.cpp, sửa Serial.print, gọi hàm gửi SMS _notifications ... */ }
/**
 * @brief Phân tích nội dung SMS để lấy số điện thoại/Zalo.
 * @param smsContent Nội dung tin nhắn SMS (chuỗi C-style).
 * @param sender Số điện thoại người gửi (chuỗi C-style).
 * @return true nếu xử lý thành công (có thể không có thay đổi), false nếu lỗi lưu EEPROM.
 */
bool parseSMSContent_notifications(const char *smsContent, const char *sender)
{
  Serial.println("Phan tich noi dung SMS:");
  // Serial.println(smsContent); // In nội dung nếu cần
  bool dataChanged = false;
  char tempBuffer[PHONE_NUMBER_SIZE_NOTIF];
  const char *keys[] = {"User:", "Phone1:", "Phone2:", "Phone3:", "Zalo1:", "Zalo2:", "Zalo3:"};
  char *targetsCharArrays[] = {userName_Notif, phone1_Notif, phone2_Notif, phone3_Notif, zalo1_Notif, zalo2_Notif, zalo3_Notif};
  int targetSizes[] = {USERNAME_SIZE_NOTIF, PHONE_NUMBER_SIZE_NOTIF, PHONE_NUMBER_SIZE_NOTIF, PHONE_NUMBER_SIZE_NOTIF, PHONE_NUMBER_SIZE_NOTIF, PHONE_NUMBER_SIZE_NOTIF, PHONE_NUMBER_SIZE_NOTIF};

  // Tách SMS thành các dòng nếu nội dung SMS có nhiều dòng
  char *smsCopy = strdup(smsContent); // Tạo bản sao để strtok không làm hỏng bản gốc
  if (smsCopy == NULL)
  {
    Serial.println("Loi cap phat bo nho cho smsCopy!");
    return false; // Lỗi nghiêm trọng
  }
  char *line = strtok(smsCopy, "\n"); // Tách theo dấu xuống dòng

  while (line != NULL)
  {
    for (int i = 0; i < 7; ++i)
    { // Duyệt qua 7 keys
      const char *p = strstr(line, keys[i]);
      if (p)
      {
        const char *valueStart = p + strlen(keys[i]);
        while (*valueStart == ' ')
          valueStart++; // Bỏ qua khoảng trắng

        if (i == 0)
        { // Xử lý User: (cho phép khoảng trắng)
          strncpy(tempBuffer, valueStart, targetSizes[i] - 1);
          tempBuffer[targetSizes[i] - 1] = '\0';
          // Loại bỏ \r hoặc \n ở cuối nếu có
          char *crlf = strpbrk(tempBuffer, "\r\n");
          if (crlf)
            *crlf = '\0';

          if (strncmp(targetsCharArrays[i], tempBuffer, targetSizes[i]) != 0)
          {
            strcpy(targetsCharArrays[i], tempBuffer); // User name
            dataChanged = true;
            Serial.printf("  Cap nhat %s %s\n", keys[i], targetsCharArrays[i]);
          }
        }
        else
        { // Xử lý PhoneX, ZaloX
          if (sscanf(valueStart, "%14s", tempBuffer) == 1)
          { // Giới hạn 14 cho SĐT
            if (strncmp(targetsCharArrays[i], tempBuffer, targetSizes[i]) != 0)
            {
              strncpy(targetsCharArrays[i], tempBuffer, targetSizes[i] - 1);
              targetsCharArrays[i][targetSizes[i] - 1] = '\0';
              dataChanged = true;
              Serial.printf("  Cap nhat %s %s\n", keys[i], targetsCharArrays[i]);
            }
          }
          else
          {
            Serial.printf("  Loi doc gia tri cho %s\n", keys[i]);
          }
        }
        goto next_line_notif_parse;
      }
    }
  next_line_notif_parse:;
    line = strtok(NULL, "\n");
  }
  free(smsCopy); // Giải phóng bộ nhớ đã cấp phát

  // Lưu vào EEPROM nếu có thay đổi
  if (dataChanged)
  {
    if (saveContactsToEEPROM_notifications())
    {
      Serial.println("Da luu thay doi vao EEPROM!");
      beep_led_notifications(3, 150);
      sendSuccessSMS_notifications(sender); // Gửi tin nhắn xác nhận
      return true;
    }
    else
    {
      Serial.println("Loi: Khong luu duoc thay doi vao EEPROM!");
      return false; // Trả về false nếu lưu lỗi
    }
  }
  else
  {
    Serial.println("Khong co thay doi trong danh ba tu SMS nay.");
    // Không cần gửi SMS xác nhận nếu không có gì thay đổi
    return true; // Vẫn coi là thành công vì đã xử lý xong SMS
  }
}
// void sendSuccessSMS_notifications(const char* recipient, const char* successMessage) { /* ... code từ sms.cpp, sửa Serial.print ... */ }
void sendSuccessSMS_notifications(const char *recipient)
{
  if (!recipient || strlen(recipient) < 9)
  {
    Serial.println("Loi: So dien thoai nguoi gui khong hop le de gui xac nhan.");
    return;
  }
  Serial.printf("Dang gui SMS xac nhan den: %s\n", recipient);
  char cmd[50];
  sprintf(cmd, "AT+CMGS=\"%s\"", recipient);

  while (simSerial_Notif.available())
    simSerial_Notif.read();
  simSerial_Notif.println(cmd);
  String response = readSerial_notifications(1000); // Chờ dấu nhắc ">"
  if (response.indexOf('>') != -1)
  {
    simSerial_Notif.print("VDetect: Da cap nhat danh ba lien he thanh cong.");
    delay(100);
    simSerial_Notif.write(0x1A);                // Ctrl+Z
    response = readSerial_notifications(10000); // Chờ phản hồi gửi
    Serial.print("Phan hoi gui SMS Xac nhan: ");
    Serial.println(response);
    if (response.indexOf("OK") != -1 || response.indexOf("+CMGS:") != -1)
    {
      Serial.println("Gui SMS xac nhan thanh cong.");
    }
    else
    {
      Serial.println("Loi: Gui SMS xac nhan that bai (sau Ctrl+Z).");
    }
  }
  else
  {
    Serial.println("Loi: Khong nhan duoc '>' de gui noi dung SMS xac nhan.");
  }
}
// void sendErrorSMS_notifications(const char* recipient, const char* errorMessage) { /* ... code từ sms.cpp, sửa Serial.print ... */ }
void sendErrorSMS_notifications(const char *recipient, const char *errorMessage)
{
  if (!recipient || strlen(recipient) < 9)
  { // Kiểm tra SĐT cơ bản
    Serial.println("Loi: So dien thoai nguoi nhan khong hop le de gui SMS.");
    return;
  }
  // Giới hạn độ dài errorMessage để tránh tràn buffer SMS (khoảng < 140 ký tự ASCII)
  char fullErrorMessage[160];
  snprintf(fullErrorMessage, sizeof(fullErrorMessage), "VDetect Canh bao: %s %s", userName_Notif, errorMessage); // Thêm tiền tố lỗi

  Serial.printf("Dang gui SMS loi den: %s - Noi dung: %s\n", recipient, fullErrorMessage);
  char cmd[50];
  sprintf(cmd, "AT+CMGS=\"%s\"", recipient);

  while (simSerial_Notif.available())
    simSerial_Notif.read(); // Xóa buffer nhận
  simSerial_Notif.println(cmd);
  String response = readSerial_notifications(1000); // Chờ dấu nhắc ">"
  if (response.indexOf('>') != -1)
  {
    simSerial_Notif.print(fullErrorMessage); // Gửi nội dung lỗi
    delay(100);
    simSerial_Notif.write(0x1A);                // Gửi Ctrl+Z
    response = readSerial_notifications(10000); // Chờ phản hồi gửi
    Serial.print("Phan hoi gui SMS Loi: ");
    Serial.println(response);
    if (response.indexOf("OK") != -1 || response.indexOf("+CMGS:") != -1)
    {
      Serial.println("Gui SMS loi thanh cong.");
    }
    else
    {
      Serial.println("Loi: Gui SMS loi that bai (sau Ctrl+Z).");
    }
  }
  else
  {
    Serial.println("Loi: Khong nhan duoc '>' de gui noi dung SMS loi.");
  }
}
// void sendLocationSMS_notifications(const char* recipient) { /* ... code từ sms.cpp, sửa Serial.print, dùng currentLat_Notif ... */ }
void sendLocationSMS_notifications(const char *recipient)
{
  char smsContent[150];

  Serial.printf("Nhan duoc yeu cau vi tri tu: %s\n", recipient);

  if (gpsFix_Notif)
  {
    // Thêm timestamp vào link nếu muốn
    // char timestamp[25];
    // sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02dZ",
    //         gps.date.year(), gps.date.month(), gps.date.day(),
    //         gps.time.hour(), gps.time.minute(), gps.time.second());
    sprintf(smsContent, "Vi tri VDetect: https://maps.google.com/maps?q=%.6f,%.6f", currentLat_Notif, currentLng_Notif);
    Serial.printf(" >> Co vi tri GPS hop le. Dang tao link: %s\n", smsContent);
  }
  else
  {
    strcpy(smsContent, "VDetect: Khong the xac dinh vi tri GPS chinh xac luc nay.");
    Serial.println(" >> Khong co vi tri GPS hop le de gui.");
  }

  // Gửi SMS đi
  Serial.printf("Dang gui SMS vi tri den: %s\n", recipient);
  char cmd[50];
  sprintf(cmd, "AT+CMGS=\"%s\"", recipient);

  while (simSerial_Notif.available())
    simSerial_Notif.read();
  simSerial_Notif.println(cmd);
  String response = readSerial_notifications(1000);
  if (response.indexOf('>') != -1)
  {
    simSerial_Notif.print(smsContent);
    delay(100);
    simSerial_Notif.write(0x1A); // Ctrl+Z
    response = readSerial_notifications(10000);
    Serial.print("Phan hoi gui SMS Vi tri: ");
    Serial.println(response);
    if (response.indexOf("OK") != -1 || response.indexOf("+CMGS:") != -1)
    {
      Serial.println("Gui SMS vi tri thanh cong.");
    }
    else
    {
      Serial.println("Loi: Gui SMS vi tri that bai (sau Ctrl+Z).");
    }
  }
  else
  {
    Serial.println("Loi: Khong nhan duoc '>' de gui noi dung SMS vi tri.");
  }
}
// void deleteAllSMS_notifications(const char* recipient) { /* ... code từ sms.cpp, sửa Serial.print ... */ }

// void updateGPS_notifications() {
//    /* ... code từ sms.cpp, sửa Serial.print, dùng gps_Notif, currentLat_Notif, ... */
// }
void updateGPS_notifications()
{
  bool newDataProcessed = false;
  unsigned long startRead = millis();
  while (gpsSerial_Notif.available() && (millis() - startRead < 75))
  { // Đọc tối đa 75ms mỗi lần gọi
    if (gps_Notif.encode(gpsSerial_Notif.read()))
    {
      newDataProcessed = true;
    }
  }

  if (newDataProcessed)
  {
    if (gps_Notif.location.isValid() && gps_Notif.location.isUpdated())
    {
      currentLat_Notif = gps_Notif.location.lat();
      currentLng_Notif = gps_Notif.location.lng();
      // Chỉ đặt gpsFix = true nếu độ chính xác (HDOP) đủ tốt (ví dụ < 3.0)
      if (gps_Notif.hdop.isValid() && gps_Notif.hdop.value() < 500)
      { // hdop value is * 100
        gpsFix_Notif = true;
        lastGpsRead_Notif = millis();
        // In log tọa độ chỉ khi thay đổi đáng kể để đỡ spam
        static float prevLat = 0.0, prevLng = 0.0;
        if (abs(currentLat_Notif - prevLat) > 0.00001 || abs(currentLng_Notif - prevLng) > 0.00001)
        {
          //  Serial.printf("GPS Update: Lat=%.6f, Lng=%.6f, Sats=%d, HDOP=%.2f\n", currentLat, currentLng, gps.satellites.value(), gps.hdop.hdop());
          prevLat = currentLat_Notif;
          prevLng = currentLng_Notif;
        }
      }
      else
      {
        // Vị trí valid nhưng độ chính xác không đủ tốt
        if (gpsFix_Notif)
        { // Nếu trước đó có fix tốt
          Serial.println("GPS HDOP too high, ignoring location.");
          // Giữ fix cũ trong thời gian ngắn? Hay đặt là false? Tạm thời đặt false.
          gpsFix_Notif = false;
        }
      }
    }
    else
    { // Dữ liệu GPS không valid
      if (gpsFix_Notif && (millis() - lastGpsRead_Notif > 10000))
      {
        gpsFix_Notif = false;
        currentLat_Notif = 0.0;
        currentLng_Notif = 0.0;
        Serial.println("!!! GPS Fix Lost (Invalid Data) !!!");
      }
    }
  }
  else
  { // Không có dữ liệu mới từ GPS
    if (gpsFix_Notif && (millis() - lastGpsRead_Notif > 30000))
    {
      gpsFix_Notif = false;
      currentLat_Notif = 0.0;
      currentLng_Notif = 0.0;
      Serial.println("!!! GPS Data Timeout - Fix Lost !!!");
    }
  }
  // In trạng thái chờ fix nếu cần
  static unsigned long lastWaitMsg = 0;
  if (!gpsFix_Notif && (millis() - lastWaitMsg > 5000))
  { // In 5 giây một lần
    // Serial.println("Waiting for valid GPS fix (HDOP < 3.0)...");
    lastWaitMsg = millis();
  }
}

// void checkAndProcessCommunications() {
//     /* ... code từ sms.cpp (là hàm checkAndProcessSIM cũ), sửa Serial.print ... */
//     /* ... dùng các hàm _notifications, các biến global _Notif, các cờ isRinging_Notif, findMeAlarmActive_Notif ... */
// }
void checkAndProcessCommunications()
{
  // --- Xử lý dữ liệu đến từ SIM ---
  while (simSerial_Notif.available())
  {
    String response = simSerial_Notif.readStringUntil('\n');
    response.trim();
    if (response.length() > 0)
    {
      Serial.print("Nhan URC/Data: ");
      Serial.println(response);

      // --- Ưu tiên xử lý Cuộc gọi ---
      if (response.indexOf("RING") != -1)
      { /* ... code xử lý RING và CLIP như phiên bản trước ... */
        if (!isRinging_Notif)
        {
          Serial.println(">>> CUOC GOI DEN <<<");
          isRinging_Notif = true;
          if (incomingCallerNumber_Notif.length() > 0 && !findMeAlarmActive_Notif && phone1_Notif[0] != '\0')
          {
            if (comparePhoneNumbers_notifications(incomingCallerNumber_Notif.c_str(), phone1_Notif))
            {
              Serial.println("   -> (RING Check, CLIP received first) Cuoc goi tu phone1. Kich hoat Tim Xe!");
              findMeAlarmActive_Notif = true;
            }
            else
            {
              Serial.println("   -> (RING Check, CLIP received first) Cuoc goi tu so khac.");
              findMeAlarmActive_Notif = false;
            }
          }
          else if (!findMeAlarmActive_Notif)
          {
            Serial.println("   -> RING den truoc CLIP, cho CLIP de xac dinh nguoi goi...");
          }
        }
        continue;
      }
      if (response.indexOf("+CLIP:") != -1)
      { /* ... code xử lý CLIP như phiên bản trước ... */
        int firstQuote = response.indexOf('"');
        if (firstQuote != -1)
        {
          int secondQuote = response.indexOf('"', firstQuote + 1);
          if (secondQuote != -1)
          {
            incomingCallerNumber_Notif = response.substring(firstQuote + 1, secondQuote);
            Serial.printf(" >> So goi den (CLIP): %s\n", incomingCallerNumber_Notif.c_str());
            if (isRinging_Notif && !findMeAlarmActive_Notif && phone1_Notif[0] != '\0')
            {
              if (comparePhoneNumbers_notifications(incomingCallerNumber_Notif.c_str(), phone1_Notif))
              {
                Serial.println("   -> (CLIP Check while Ringing) Cuoc goi tu phone1. Kich hoat Tim Xe!");
                findMeAlarmActive_Notif = true;
              }
              else
              {
                Serial.println("   -> (CLIP Check while Ringing) Cuoc goi tu so khac.");
              }
            }
          }
        }
        continue;
      }
      if (isRinging_Notif && (response.indexOf("NO CARRIER") != -1 || response.indexOf("BUSY") != -1 || response.indexOf("NO ANSWER") != -1 || response.indexOf("VOICE CALL: END") != -1))
      {
        Serial.println(">>> CUOC GOI KET THUC <<<");
        isRinging_Notif = false;
        findMeAlarmActive_Notif = false;
        incomingCallerNumber_Notif = "";
        continue;
      }
      if (isRinging_Notif || findMeAlarmActive_Notif)
        continue; // Bỏ qua xử lý SMS nếu đang gọi hoặc đang tìm xe

      // --- Xử lý SMS mới (+CMTI) ---
      int cmtIndex = response.indexOf("+CMTI:");
      if (cmtIndex != -1)
      {
        int smsIndex = -1; /* ... code parse smsIndex ... */
        int commaPos = response.indexOf(',');
        int quotePos = response.indexOf('"', cmtIndex);
        if (commaPos > quotePos && quotePos != -1)
        {
          smsIndex = response.substring(commaPos + 1).toInt();
        }
        else
        {
          const char *p = response.c_str() + cmtIndex + strlen("+CMTI:");
          while (*p && (*p != ','))
            p++;
          if (*p == ',')
            smsIndex = atoi(p + 1);
        }

        if (smsIndex > 0)
        {
          Serial.printf("Co tin nhan moi (index: %d)\n", smsIndex);
          beep_led_notifications(2, 100);

          // --- Đọc SMS ---
          char cmd[20];
          sprintf(cmd, "AT+CMGR=%d", smsIndex);
          // ... (Code đọc SMS nhiều dòng vào smsBody và senderNumLocal như cũ) ...
          Serial.printf("Gui lenh doc SMS: %s\n", cmd);
          while (simSerial_Notif.available())
            simSerial_Notif.read();
          simSerial_Notif.println(cmd);
          String smsFullResponse = "";
          String smsBody = "";
          String senderNumLocal = "";
          bool headerFound = false;
          bool bodyStarted = false;
          unsigned long readStart = millis();
          bool okReceived = false;
          Serial.println("--- Bat dau doc phan hoi CMGR ---");
          while (millis() - readStart < 5000 && !okReceived)
          { /* ... code đọc ... */
            if (simSerial_Notif.available())
            {
              String line = simSerial_Notif.readStringUntil('\n');
              line.trim();
              if (line.length() > 0)
              {
                smsFullResponse += line + "\n";
                if (line.startsWith("+CMGR:"))
                {
                  headerFound = true;
                  bodyStarted = false;
                  int firstComma = line.indexOf(',');
                  int secondComma = line.indexOf(',', firstComma + 1);
                  if (firstComma != -1 && secondComma != -1)
                  {
                    int start = line.indexOf('"', firstComma) + 1;
                    int end = line.indexOf('"', start);
                    if (start != 0 && end != -1 && (end - start) < PHONE_NUMBER_SIZE_NOTIF)
                    {
                      senderNumLocal = line.substring(start, end);
                    }
                  }
                  if (senderNumLocal.length() > 0)
                    Serial.printf("   Nguoi gui (tu CMGR): %s\n", senderNumLocal.c_str());
                }
                else if (headerFound && line.equals("OK"))
                {
                  okReceived = true;
                  bodyStarted = false;
                }
                else if (headerFound)
                {
                  smsBody += line + "\n";
                  bodyStarted = true;
                }
                else if (line.equals("OK"))
                {
                  okReceived = true;
                }
                else if (line.indexOf("ERROR") != -1)
                {
                  okReceived = true;
                }
              }
              readStart = millis();
            }
            delay(10);
          }
          Serial.println("--- Ket thuc doc phan hoi CMGR ---");
          if (smsBody.endsWith("\n"))
            smsBody.remove(smsBody.length() - 1);
          if (!okReceived)
            Serial.println("Loi: Timeout/Loi khi doc SMS tu module.");
          // --- Kết thúc đọc SMS ---

          // --- Xử lý nội dung SMS đã đọc ---
          if (smsBody.length() > 0 || (okReceived && headerFound && smsBody.length() == 0))
          {
            if (smsBody.length() > 0)
            {
              Serial.println("--- Noi dung SMS Body Trich Xuat ---\n" + smsBody + "\n----------------------------------");
            }
            else
            {
              Serial.println("--- Noi dung SMS rong ---");
            }

            // *** SỬA LOGIC XỬ LÝ SMS VÀ THÊM BÁO LỖI ***
            bool processed = false; // Cờ đánh dấu SMS đã được xử lý hay chưa

            // 1. Ưu tiên kiểm tra lệnh "Vi tri" (không dấu)
            if (smsBody.equalsIgnoreCase("Vi tri"))
            {
              processed = true; // Đánh dấu đã xử lý (hoặc cố gắng xử lý) lệnh này

              // Kiểm tra người gửi có phải phone1 không
              Serial.print("So phone1_Notif: [");
              Serial.print(phone1_Notif);
              Serial.println("]");
              Serial.print("So senderNumLocal: [");
              Serial.print(senderNumLocal);
              Serial.println("]");
              if (senderNumLocal.length() > 0 && phone1_Notif[0] != '\0' && comparePhoneNumbers_notifications(senderNumLocal.c_str(), phone1_Notif))
              {
                // ĐÚNG: Lệnh đúng, người gửi đúng
                Serial.println(" >> Nhan yeu cau 'Vi tri' tu chu nhan (phone1).");
                sendLocationSMS_notifications(phone1_Notif); // Gọi hàm gửi vị trí
              }
              // *** THÊM: Xử lý lỗi sai người gửi ***
              else if (senderNumLocal.length() > 0)
              {
                // SAI NGƯỜI GỬI: Lệnh đúng nhưng người gửi không phải phone1
                Serial.println(" >> LOI: Nhan yeu cau 'Vi tri' nhung KHONG PHAI tu phone1.");
                // Gửi SMS báo lỗi về cho người gửi sai
                sendErrorSMS_notifications(senderNumLocal.c_str(), "Lenh 'Vi tri' chi chap nhan tu so Phone1 da cau hinh.");
              }
              // *** KẾT THÚC THÊM ***
              else
              {
                // Lệnh đúng nhưng không xác định được người gửi?
                Serial.println(" >> Nhan yeu cau 'Vi tri' nhung khong xac dinh duoc nguoi gui.");
              }
            }

            // 2. Nếu không phải lệnh "Vi tri", thử xử lý như SMS cấu hình
            if (!processed && senderNumLocal.length() > 0)
            {
              processed = true;
              Serial.println(" >> Xu ly nhu SMS cau hinh.");
              bool parseResult = parseSMSContent_notifications(smsBody.c_str(), senderNumLocal.c_str());
              if (!parseResult)
              {
                // Giả sử lỗi duy nhất là EEPROM
                Serial.println(" >> LOI: Luu cau hinh vao EEPROM that bai.");
                sendErrorSMS_notifications(senderNumLocal.c_str(), "Loi luu cau hinh vao bo nho.");
              }
            }

            // 3. Các trường hợp SMS không được xử lý khác
            if (!processed)
            {
              Serial.println(" >> SMS khong duoc xu ly (lenh khong hop le hoac khong co nguoi gui).");
              // Tùy chọn: Gửi lỗi về người gửi nếu xác định được senderNumLocal
              // if (senderNumLocal.length() > 0) {
              //    sendErrorSMS_notifications(senderNumLocal.c_str(), "Lenh hoac noi dung tin nhan khong hop le.");
              // }
            }
          }
          else
          {
            Serial.println("Khong trich xuat duoc noi dung SMS (do loi doc).");
          }
          // ------------------------------------

          // Xóa SMS sau khi xử lý
          Serial.printf("Gui lenh xoa SMS index %d\n", smsIndex);
          sprintf(cmd, "AT+CMGD=%d", smsIndex);
          if (sendCommand_notifications(cmd, 5000, true))
          {
            Serial.printf("Da xoa SMS index %d.\n", smsIndex);
          }
          else
          {
            Serial.printf("Loi/Khong xoa duoc SMS index %d.\n", smsIndex);
          }
        }
        else
        {
          Serial.println("Khong the trich xuat index SMS tu +CMTI.");
        }
      } // Kết thúc xử lý +CMTI
    } // Kết thúc if (response.length() > 0)
  } // Kết thúc while (simSerial.available())

  // Phần kiểm tra isRinging cuối hàm để tắt còi/LED nếu cần
  // Sẽ được handleFindMeAlarm xử lý nên có thể bỏ qua ở đây
}

float getGPSLat_notifications()
{
  return currentLat_Notif;
}
float getGPSLng_notifications()
{
  return currentLng_Notif;
}
bool isGPSFixed_notifications()
{
  return gpsFix_Notif;
}
const char *getUserName_notifications()
{
  return userName_Notif;
}
const char *getPhone1Num_notifications()
{
  return phone1_Notif;
}
bool isFindMeAlarmActive_notifications()
{
  return findMeAlarmActive_Notif;
}
void setFindMeAlarmActive_notifications(bool state)
{ // Thêm Serial
  if (findMeAlarmActive_Notif != state)
  {
    findMeAlarmActive_Notif = state;
    if (state)
    {
      Serial.println("FindMe Alarm ACTIVATED from notifications module.");
    }
    else
    {
      Serial.println("FindMe Alarm DEACTIVATED from notifications module.");
    }
  }
}
bool isDeviceRinging_notifications()
{
  return isRinging_Notif;
}
