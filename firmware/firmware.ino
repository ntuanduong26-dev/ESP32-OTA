#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "version.h"
#include "secrets.h"

// =====================================================
// Wi-Fi
// =====================================================
// const char* WIFI_SSID = "Ha Noi Med T4";
// const char* WIFI_PASSWORD = "21156ltt";

// // =====================================================
// // Thông tin phiên bản và địa chỉ OTA
// // =====================================================

// // Phiên bản hiện đang chạy trong ESP32
// // const char* CURRENT_VERSION = "1.0.0";
// const char* CURRENT_VERSION = "1.0.1";
const char* WIFI_SSID = WIFI_SSID_VALUE;
const char* WIFI_PASSWORD = WIFI_PASSWORD_VALUE;

const char* CURRENT_VERSION = FW_VERSION;

// GitHub Pages của bạn
const char* VERSION_URL =
  "https://ntuanduong26-dev.github.io/ESP32-OTA/version.txt";

const char* FIRMWARE_URL =
  "https://ntuanduong26-dev.github.io/ESP32-OTA/firmware.bin";

// Sau khi bật ESP32, chờ 10 giây rồi kiểm tra lần đầu
const unsigned long THOI_GIAN_CHO_KIEM_TRA_DAU = 10000;

// Sau đó kiểm tra lại mỗi 5 phút
const unsigned long CHU_KY_KIEM_TRA_OTA =
  5UL * 60UL * 1000UL;

// =====================================================
// Cảm biến ánh sáng
// =====================================================
const int CAM_BIEN_SANG = 21;

// Cảm biến của bạn đang dùng:
// trời tối D0 = HIGH
const int TRANG_THAI_TOI = HIGH;

// =====================================================
// LED
// =====================================================
const int ledPins[] = {14, 27, 26, 25, 33};

const int SO_LED =
  sizeof(ledPins) / sizeof(ledPins[0]);

const unsigned long THOI_GIAN_CHAY = 1000;

// Trạng thái hiệu ứng LED
int viTriLed = 0;
int huongChay = 1;

bool dangChayLed = false;

unsigned long thoiGianLedTruoc = 0;
unsigned long thoiGianKiemTraOTATruoc = 0;
unsigned long thoiGianThuLaiWiFi = 0;

bool daKiemTraOTALanDau = false;

// =====================================================
// Điều khiển LED
// =====================================================

void tatTatCaLed() {
  for (int i = 0; i < SO_LED; i++) {
    digitalWrite(ledPins[i], LOW);
  }
}

void hienThiMotLed(int viTri) {
  tatTatCaLed();

  if (viTri >= 0 && viTri < SO_LED) {
    digitalWrite(ledPins[viTri], HIGH);
  }
}

void capNhatHieuUngLed() {
  bool troiToi =
    digitalRead(CAM_BIEN_SANG) == TRANG_THAI_TOI;

  // Trời sáng: tắt toàn bộ LED
  if (!troiToi) {
    tatTatCaLed();

    viTriLed = 0;
    huongChay = 1;
    dangChayLed = false;

    return;
  }

  // Vừa chuyển sang trời tối
  if (!dangChayLed) {
    dangChayLed = true;

    viTriLed = 0;
    huongChay = 1;

    hienThiMotLed(viTriLed);
    thoiGianLedTruoc = millis();

    return;
  }

  // Dùng millis thay vì delay để chương trình không bị chặn
  if (millis() - thoiGianLedTruoc >= THOI_GIAN_CHAY) {
    thoiGianLedTruoc = millis();

    viTriLed += huongChay;

    // Đến bên phải thì quay lại
    if (viTriLed >= SO_LED - 1) {
      viTriLed = SO_LED - 1;
      huongChay = -1;
    }

    // Đến bên trái thì chạy sang phải
    else if (viTriLed <= 0) {
      viTriLed = 0;
      huongChay = 1;
    }

    hienThiMotLed(viTriLed);
  }
}

// =====================================================
// Kết nối Wi-Fi
// =====================================================

bool ketNoiWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.println();
  Serial.print("Dang ket noi Wi-Fi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long batDau = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    if (millis() - batDau >= 20000) {
      Serial.println();
      Serial.println("Khong ket noi duoc Wi-Fi.");

      return false;
    }
  }

  // Hạn chế Wi-Fi ngủ trong lúc tải firmware
  WiFi.setSleep(false);

  Serial.println();
  Serial.println("Da ket noi Wi-Fi.");

  Serial.print("Dia chi IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("Phien ban hien tai: ");
  Serial.println(CURRENT_VERSION);

  return true;
}

void duyTriKetNoiWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  // Thử kết nối lại mỗi 10 giây
  if (millis() - thoiGianThuLaiWiFi >= 10000) {
    thoiGianThuLaiWiFi = millis();

    Serial.println("Wi-Fi bi mat. Dang ket noi lai...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// =====================================================
// Xử lý phiên bản
// =====================================================

bool tachPhienBan(
  const String& version,
  int& major,
  int& minor,
  int& patch
) {
  major = 0;
  minor = 0;
  patch = 0;

  int ketQua = sscanf(
    version.c_str(),
    "%d.%d.%d",
    &major,
    &minor,
    &patch
  );

  return ketQua == 3;
}

bool laPhienBanMoiHon(
  const String& phienBanMoi,
  const String& phienBanHienTai
) {
  int newMajor;
  int newMinor;
  int newPatch;

  int currentMajor;
  int currentMinor;
  int currentPatch;

  if (!tachPhienBan(
        phienBanMoi,
        newMajor,
        newMinor,
        newPatch
      )) {
    Serial.println("Phien ban tren server khong hop le.");
    return false;
  }

  if (!tachPhienBan(
        phienBanHienTai,
        currentMajor,
        currentMinor,
        currentPatch
      )) {
    Serial.println("CURRENT_VERSION khong hop le.");
    return false;
  }

  if (newMajor != currentMajor) {
    return newMajor > currentMajor;
  }

  if (newMinor != currentMinor) {
    return newMinor > currentMinor;
  }

  return newPatch > currentPatch;
}

// =====================================================
// Đọc version.txt
// =====================================================

String docPhienBanTuServer() {
  if (WiFi.status() != WL_CONNECTED) {
    return "";
  }

  NetworkClientSecure client;

  /*
    Dùng để thử nghiệm:
    vẫn mã hóa HTTPS nhưng không xác minh chứng chỉ server.

    Sau khi chạy ổn, nên thay bằng Root CA để bảo mật đúng.
  */
  client.setInsecure();

  HTTPClient http;

  // Thêm tham số để hạn chế lấy file version.txt từ cache
  String url =
    String(VERSION_URL) +
    "?time=" +
    String(millis());

  Serial.print("Dang doc phien ban: ");
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("Khong khoi tao duoc ket noi HTTPS.");
    return "";
  }

  http.setConnectTimeout(10000);
  http.setTimeout(10000);

  // Cho phép theo redirect nếu server chuyển hướng
  http.setFollowRedirects(
    HTTPC_STRICT_FOLLOW_REDIRECTS
  );

  http.addHeader("Cache-Control", "no-cache");

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.print("Loi doc version.txt. HTTP code: ");
    Serial.println(httpCode);

    http.end();
    return "";
  }

  String phienBan = http.getString();
  phienBan.trim();

  http.end();

  Serial.print("Phien ban tren GitHub: ");
  Serial.println(phienBan);

  return phienBan;
}

// =====================================================
// Tải và cài firmware.bin
// =====================================================

void capNhatFirmware(const String& phienBanMoi) {
  Serial.println();
  Serial.println("================================");
  Serial.println("BAT DAU CAP NHAT FIRMWARE");
  Serial.print("Tu phien ban: ");
  Serial.println(CURRENT_VERSION);

  Serial.print("Len phien ban: ");
  Serial.println(phienBanMoi);
  Serial.println("================================");

  tatTatCaLed();

  NetworkClientSecure client;

  // Chỉ dùng để thử nghiệm ban đầu
  client.setInsecure();
  client.setTimeout(20000);

  httpUpdate.rebootOnUpdate(true);

  httpUpdate.setFollowRedirects(
    HTTPC_STRICT_FOLLOW_REDIRECTS
  );

  httpUpdate.onStart([]() {
    Serial.println("Dang bat dau tai firmware...");
  });

  httpUpdate.onProgress([](int current, int total) {
    if (total > 0) {
      int phanTram =
        static_cast<int>(
          (current * 100LL) / total
        );

      Serial.printf(
        "Tien trinh: %d%%\r",
        phanTram
      );
    }
  });

  httpUpdate.onEnd([]() {
    Serial.println();
    Serial.println("Da ghi firmware thanh cong.");
    Serial.println("ESP32 se khoi dong lai.");
  });

  httpUpdate.onError([](int error) {
    Serial.println();
    Serial.print("Loi HTTP OTA: ");
    Serial.println(error);

    Serial.print("Chi tiet: ");
    Serial.println(
      httpUpdate
        .getLastErrorString()
    );
  });

  // Thêm version vào URL để hạn chế dùng firmware cũ từ cache
  String firmwareUrl =
    String(FIRMWARE_URL) +
    "?version=" +
    phienBanMoi;

  Serial.print("Dang tai firmware tu: ");
  Serial.println(firmwareUrl);

  t_httpUpdate_return ketQua =
    httpUpdate.update(
      client,
      firmwareUrl,
      CURRENT_VERSION
    );

  switch (ketQua) {
    case HTTP_UPDATE_FAILED:
      Serial.println();
      Serial.println("Cap nhat firmware that bai.");
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println();
      Serial.println("Server bao khong co ban cap nhat.");
      break;

    case HTTP_UPDATE_OK:
      /*
        Thông thường không chạy tới đây vì ESP32
        tự khởi động lại ngay sau khi cập nhật thành công.
      */
      Serial.println();
      Serial.println("Cap nhat thanh cong.");
      break;
  }
}

// =====================================================
// Kiểm tra cập nhật
// =====================================================

void kiemTraCapNhatOTA() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Khong the kiem tra OTA vi mat Wi-Fi.");
    return;
  }

  Serial.println();
  Serial.println("----- KIEM TRA OTA -----");

  Serial.print("Phien ban ESP32: ");
  Serial.println(CURRENT_VERSION);

  String phienBanServer = docPhienBanTuServer();

  if (phienBanServer.length() == 0) {
    Serial.println("Khong doc duoc phien ban tu server.");
    return;
  }

  if (
    laPhienBanMoiHon(
      phienBanServer,
      CURRENT_VERSION
    )
  ) {
    Serial.println("Phat hien phien ban moi.");
    capNhatFirmware(phienBanServer);
  } else {
    Serial.println("ESP32 dang o phien ban moi nhat.");
  }
}

// =====================================================
// Setup
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(CAM_BIEN_SANG, INPUT);

  for (int i = 0; i < SO_LED; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }

  tatTatCaLed();

  Serial.println();
  Serial.println("ESP32 bat dau khoi dong.");

  ketNoiWiFi();
}

// =====================================================
// Loop
// =====================================================

void loop() {
  capNhatHieuUngLed();
  duyTriKetNoiWiFi();

  unsigned long hienTai = millis();

  // Kiểm tra OTA lần đầu sau 10 giây
  if (
    !daKiemTraOTALanDau &&
    hienTai >= THOI_GIAN_CHO_KIEM_TRA_DAU
  ) {
    daKiemTraOTALanDau = true;
    thoiGianKiemTraOTATruoc = hienTai;

    kiemTraCapNhatOTA();
  }

  // Kiểm tra lại định kỳ
  if (
    daKiemTraOTALanDau &&
    hienTai - thoiGianKiemTraOTATruoc
      >= CHU_KY_KIEM_TRA_OTA
  ) {
    thoiGianKiemTraOTATruoc = hienTai;

    kiemTraCapNhatOTA();
  }

  delay(1);
}

