// notifications.h
#ifndef NOTIFICATIONS_H
#define NOTIFICATIONS_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <EEPROM.h>
#include <TinyGPS++.h>

// --- CẤU HÌNH CHUNG (Có thể điều chỉnh ở file .ino chính nếu muốn) ---
#define LED_PIN_NOTIF      19 // Chân LED (Dùng chung với MPU nếu LED_PIN trong .ino giống)
#define BUZZER_PIN_NOTIF   18 // Chân Còi (Dùng chung với MPU nếu BUZZER_PIN trong .ino giống)

// --- CẤU HÌNH CHÂN SIM VÀ GPS (Từ sms.cpp của bạn) ---
#define SIM_ENABLE_PIN_NOTIF     15
#define SIM_SERIAL_RX_PIN_NOTIF  26
#define SIM_SERIAL_TX_PIN_NOTIF  27
#define GPS_SERIAL_RX_PIN_NOTIF  16
#define GPS_SERIAL_TX_PIN_NOTIF  17

// --- CẤU HÌNH MODULE SIM VÀ GPS ---
#define SIM_BAUD_RATE_NOTIF      115200
#define GPS_BAUD_RATE_NOTIF      9600

// --- CẤU HÌNH EEPROM ---
#define USERNAME_SIZE_NOTIF      30
#define EEPROM_SIZE_NOTIF        230
#define EEPROM_MAGIC_ADDR_NOTIF  0
#define EEPROM_DATA_ADDR_NOTIF   1
#define EEPROM_MAGIC_VALUE_NOTIF 0xAB

// --- KHÁC ---
#define PHONE_NUMBER_SIZE_NOTIF 15
#define SIM_INIT_TIMEOUT_NOTIF 30000
#define SIM_PIN_TIMEOUT_NOTIF  15000

// --- KHAI BÁO EXTERN CHO CÁC BIẾN TOÀN CỤC ---
// Các biến này sẽ được ĐỊNH NGHĨA trong notifications.cpp
extern char userName_Notif[USERNAME_SIZE_NOTIF];
extern HardwareSerial simSerial_Notif;
extern HardwareSerial gpsSerial_Notif;
extern TinyGPSPlus gps_Notif;

extern char phone1_Notif[PHONE_NUMBER_SIZE_NOTIF];
extern char phone2_Notif[PHONE_NUMBER_SIZE_NOTIF];
extern char phone3_Notif[PHONE_NUMBER_SIZE_NOTIF];
extern char zalo1_Notif[PHONE_NUMBER_SIZE_NOTIF];
extern char zalo2_Notif[PHONE_NUMBER_SIZE_NOTIF];
extern char zalo3_Notif[PHONE_NUMBER_SIZE_NOTIF];

extern float currentLat_Notif;
extern float currentLng_Notif;
extern bool gpsFix_Notif;
extern unsigned long lastGpsRead_Notif;

extern bool isRinging_Notif;
extern bool findMeAlarmActive_Notif; // Cờ cho chức năng Tìm Xe
extern String incomingCallerNumber_Notif;

// --- KHAI BÁO CÁC HÀM (PROTOTYPES) ---
// Hàm tiện ích chung
void beep_led_notifications(int count, int duration_ms = 100, int ledPin = LED_PIN_NOTIF, int buzzerPin = BUZZER_PIN_NOTIF);

// Hàm khởi tạo
void initNotifications();
bool initializeSIM_notifications();

// Hàm giao tiếp AT command
String sendCommand_notifications(const char* cmd, unsigned long timeout = 3000, bool requireOK = true, bool printResponse = true);
String readSerial_notifications(unsigned long timeout, HardwareSerial& commSerial);

// Hàm EEPROM
void loadContactsFromEEPROM_notifications();
bool saveContactsToEEPROM_notifications();

// Hàm xử lý SĐT và SMS
bool comparePhoneNumbers_notifications(const char* num1, const char* num2);
bool parseSMSContent_notifications(const char* smsBody, const char* senderNum);
void sendSuccessSMS_notifications(const char* recipient);
void sendErrorSMS_notifications(const char* recipient, const char* errorMessage);
void sendLocationSMS_notifications(const char* recipient);
void deleteAllSMS_notifications(const char* recipient);

// Hàm xử lý chính cho SIM/SMS/Call và GPS
void checkAndProcessCommunications();
void updateGPS_notifications();

// Hàm Getter/Setter cho trạng thái (để file .ino tương tác)
float getGPSLat_notifications();
float getGPSLng_notifications();
bool isGPSFixed_notifications();
const char* getUserName_notifications();
const char* getPhone1Num_notifications();
bool isFindMeAlarmActive_notifications();
void setFindMeAlarmActive_notifications(bool state); // Thêm debugSerial
bool isDeviceRinging_notifications();
// void setDeviceRinging_notifications(bool state); // isRinging sẽ do checkAndProcessCommunications quản lý nội bộ

#endif // NOTIFICATIONS_H