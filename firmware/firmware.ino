#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

#include "version.h"
#include "secrets.h"  // Chỉ dùng cho bản chuyển tiếp 1.0.2

// =====================================================
// OTA
// =====================================================

const char* CURRENT_VERSION = FW_VERSION;

const char* VERSION_URL =
  "https://ntuanduong26-dev.github.io/ESP32-OTA/version.txt";

const char* FIRMWARE_URL =
  "https://ntuanduong26-dev.github.io/ESP32-OTA/firmware.bin";

const unsigned long OTA_CHECK_FIRST = 10000;
const unsigned long OTA_CHECK_INTERVAL = 5UL * 60UL * 1000UL;

// =====================================================
// Trang cấu hình Wi-Fi
// =====================================================

// ESP32 sẽ phát mạng này nếu chưa có Wi-Fi
// hoặc không kết nối được Wi-Fi đã lưu.
const char* CONFIG_AP_SSID = "ESP32-SETUP";
const char* CONFIG_AP_PASSWORD = "12345678";

const int CONFIG_BUTTON_PIN = 0;  // Nút BOOT

Preferences preferences;
WebServer configServer(80);

bool configPortalActive = false;

unsigned long buttonPressedAt = 0;
bool buttonActionHandled = false;

unsigned long wifiDisconnectedAt = 0;
unsigned long lastReconnectAttempt = 0;

// =====================================================
// Cảm biến và LED
// =====================================================

const int CAM_BIEN_SANG = 21;

const int ledPins[] = {14, 27, 26, 25, 33};
const int SO_LED = sizeof(ledPins) / sizeof(ledPins[0]);

const int TRANG_THAI_TOI = HIGH;

const unsigned long THOI_GIAN_CHAY = 100;

int viTriLed = 0;
int huongChay = 1;
bool dangChayLed = false;

unsigned long thoiGianLedTruoc = 0;

// =====================================================
// Bộ đếm OTA
// =====================================================

unsigned long thoiGianKiemTraOTATruoc = 0;
bool daKiemTraOTALanDau = false;

// =====================================================
// Lưu và đọc Wi-Fi trong NVS
// =====================================================

bool docWiFiTuNVS(String& ssid, String& password) {
  preferences.begin("wifi-config", true);

  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");

  preferences.end();

  return ssid.length() > 0;
}

bool luuWiFiVaoNVS(
  const String& ssid,
  const String& password
) {
  if (ssid.length() == 0 || ssid.length() > 32) {
    return false;
  }

  if (password.length() > 63) {
    return false;
  }

  preferences.begin("wifi-config", false);

  bool okSsid =
    preferences.putString("ssid", ssid) > 0;

  // Mạng mở có thể có mật khẩu rỗng nên không kiểm tra > 0
  preferences.putString("password", password);

  preferences.end();

  return okSsid;
}

void xoaWiFiTrongNVS() {
  preferences.begin("wifi-config", false);
  preferences.clear();
  preferences.end();

  Serial.println("Da xoa cau hinh Wi-Fi trong NVS.");
}

// =====================================================
// Chuyển Wi-Fi hiện tại vào NVS đúng một lần
// =====================================================

void chuyenWiFiHienTaiVaoNVS() {
  String ssidDaLuu;
  String passwordDaLuu;

  // Nếu NVS đã có Wi-Fi thì không làm gì
  if (docWiFiTuNVS(ssidDaLuu, passwordDaLuu)) {
    Serial.print("NVS da co Wi-Fi: ");
    Serial.println(ssidDaLuu);
    return;
  }

  String ssidChuyenTiep = WIFI_SSID_VALUE;
  String passwordChuyenTiep = WIFI_PASSWORD_VALUE;

  if (ssidChuyenTiep.length() == 0) {
    Serial.println("Khong co Wi-Fi chuyen tiep.");
    return;
  }

  if (
    luuWiFiVaoNVS(
      ssidChuyenTiep,
      passwordChuyenTiep
    )
  ) {
    Serial.print("Da luu Wi-Fi hien tai vao NVS: ");
    Serial.println(ssidChuyenTiep);
  } else {
    Serial.println("Khong luu duoc Wi-Fi vao NVS.");
  }
}

// =====================================================
// Trang cấu hình Wi-Fi
// =====================================================

String taoTrangCauHinhWiFi() {
  String html;

  html += R"HTML(
<!doctype html>
<html lang="vi">
<head>
  <meta charset="utf-8">
  <meta name="viewport"
        content="width=device-width, initial-scale=1">
  <title>Cấu hình Wi-Fi ESP32</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      max-width: 460px;
      margin: 40px auto;
      padding: 20px;
      background: #f4f4f4;
    }

    .box {
      background: white;
      padding: 24px;
      border-radius: 12px;
      box-shadow: 0 2px 12px rgba(0,0,0,.12);
    }

    input {
      width: 100%;
      box-sizing: border-box;
      padding: 12px;
      margin: 7px 0 16px;
      font-size: 16px;
    }

    button {
      width: 100%;
      padding: 13px;
      font-size: 16px;
      cursor: pointer;
    }

    .note {
      color: #555;
      font-size: 14px;
    }
  </style>
</head>

<body>
  <div class="box">
    <h2>Cấu hình Wi-Fi cho ESP32</h2>

    <form method="POST" action="/save">
      <label>Tên Wi-Fi</label>
      <input
        name="ssid"
        maxlength="32"
        required
        autocomplete="off">

      <label>Mật khẩu Wi-Fi</label>
      <input
        name="password"
        type="password"
        maxlength="63">

      <button type="submit">
        Lưu và kết nối
      </button>
    </form>

    <p class="note">
      Sau khi lưu, ESP32 sẽ khởi động lại và kết nối
      vào mạng Wi-Fi vừa nhập.
    </p>
  </div>
</body>
</html>
)HTML";

  return html;
}

void xuLyTrangChu() {
  configServer.send(
    200,
    "text/html; charset=utf-8",
    taoTrangCauHinhWiFi()
  );
}

void xuLyLuuWiFi() {
  if (!configServer.hasArg("ssid")) {
    configServer.send(
      400,
      "text/plain; charset=utf-8",
      "Thiếu tên Wi-Fi."
    );

    return;
  }

  String ssid = configServer.arg("ssid");
  String password = configServer.arg("password");

  if (
    ssid.length() == 0 ||
    ssid.length() > 32 ||
    password.length() > 63
  ) {
    configServer.send(
      400,
      "text/plain; charset=utf-8",
      "Tên hoặc mật khẩu Wi-Fi không hợp lệ."
    );

    return;
  }

  if (!luuWiFiVaoNVS(ssid, password)) {
    configServer.send(
      500,
      "text/plain; charset=utf-8",
      "Không lưu được cấu hình Wi-Fi."
    );

    return;
  }

  configServer.send(
    200,
    "text/html; charset=utf-8",
    R"HTML(
<!doctype html>
<html lang="vi">
<head>
  <meta charset="utf-8">
  <meta name="viewport"
        content="width=device-width, initial-scale=1">
</head>
<body style="font-family:Arial;text-align:center;padding:40px">
  <h2>Đã lưu Wi-Fi</h2>
  <p>ESP32 đang khởi động lại...</p>
</body>
</html>
)HTML"
  );

  Serial.print("Da nhan Wi-Fi moi: ");
  Serial.println(ssid);

  delay(1500);
  ESP.restart();
}

void batTrangCauHinhWiFi() {
  if (configPortalActive) {
    return;
  }

  configPortalActive = true;

  WiFi.disconnect();
  delay(200);

  WiFi.mode(WIFI_AP);

  bool apStarted =
    WiFi.softAP(
      CONFIG_AP_SSID,
      CONFIG_AP_PASSWORD
    );

  if (!apStarted) {
    Serial.println("Khong bat duoc Wi-Fi cau hinh.");
    return;
  }

  configServer.on("/", HTTP_GET, xuLyTrangChu);
  configServer.on("/save", HTTP_POST, xuLyLuuWiFi);

  configServer.onNotFound([]() {
    configServer.sendHeader("Location", "/");
    configServer.send(302, "text/plain", "");
  });

  configServer.begin();

  Serial.println();
  Serial.println("===== CHE DO CAU HINH WI-FI =====");

  Serial.print("Ten Wi-Fi: ");
  Serial.println(CONFIG_AP_SSID);

  Serial.print("Mat khau: ");
  Serial.println(CONFIG_AP_PASSWORD);

  Serial.print("Mo trinh duyet tai: http://");
  Serial.println(WiFi.softAPIP());

  Serial.println("=================================");
}

// =====================================================
// Kết nối Wi-Fi đã lưu
// =====================================================

bool ketNoiWiFiDaLuu() {
  String ssid;
  String password;

  if (!docWiFiTuNVS(ssid, password)) {
    Serial.println("NVS chua co thong tin Wi-Fi.");
    return false;
  }

  Serial.print("Dang ket noi Wi-Fi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long batDau = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - batDau < 20000
  ) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Khong ket noi duoc Wi-Fi da luu.");
    return false;
  }

  WiFi.setSleep(false);

  Serial.println("Da ket noi Wi-Fi.");

  Serial.print("Dia chi IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("Phien ban hien tai: ");
  Serial.println(CURRENT_VERSION);

  return true;
}

void duyTriWiFi() {
  if (
    configPortalActive ||
    WiFi.status() == WL_CONNECTED
  ) {
    wifiDisconnectedAt = 0;
    return;
  }

  unsigned long hienTai = millis();

  if (wifiDisconnectedAt == 0) {
    wifiDisconnectedAt = hienTai;
  }

  if (
    hienTai - lastReconnectAttempt >= 10000
  ) {
    lastReconnectAttempt = hienTai;

    Serial.println("Thu ket noi lai Wi-Fi...");
    WiFi.reconnect();
  }

  // Mất Wi-Fi liên tục 60 giây thì mở trang cấu hình
  if (
    hienTai - wifiDisconnectedAt >= 60000
  ) {
    Serial.println(
      "Mat Wi-Fi qua lau. Bat che do cau hinh."
    );

    batTrangCauHinhWiFi();
  }
}

// =====================================================
// Giữ nút BOOT 5 giây để đổi Wi-Fi
// =====================================================

void xuLyNutCauHinh() {
  bool dangNhan =
    digitalRead(CONFIG_BUTTON_PIN) == LOW;

  if (dangNhan) {
    if (buttonPressedAt == 0) {
      buttonPressedAt = millis();
    }

    if (
      !buttonActionHandled &&
      millis() - buttonPressedAt >= 5000
    ) {
      buttonActionHandled = true;

      Serial.println();
      Serial.println(
        "Giu BOOT 5 giay: xoa Wi-Fi va mo cau hinh."
      );

      xoaWiFiTrongNVS();
      batTrangCauHinhWiFi();
    }
  } else {
    buttonPressedAt = 0;
    buttonActionHandled = false;
  }
}

// =====================================================
// LED
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

  if (!troiToi) {
    tatTatCaLed();

    viTriLed = 0;
    huongChay = 1;
    dangChayLed = false;

    return;
  }

  if (!dangChayLed) {
    dangChayLed = true;

    viTriLed = 0;
    huongChay = 1;

    hienThiMotLed(viTriLed);
    thoiGianLedTruoc = millis();

    return;
  }

  if (
    millis() - thoiGianLedTruoc >=
    THOI_GIAN_CHAY
  ) {
    thoiGianLedTruoc = millis();

    viTriLed += huongChay;

    if (viTriLed >= SO_LED - 1) {
      viTriLed = SO_LED - 1;
      huongChay = -1;
    } else if (viTriLed <= 0) {
      viTriLed = 0;
      huongChay = 1;
    }

    hienThiMotLed(viTriLed);
  }
}

// =====================================================
// So sánh phiên bản
// =====================================================

bool tachPhienBan(
  const String& version,
  int& major,
  int& minor,
  int& patch
) {
  return sscanf(
    version.c_str(),
    "%d.%d.%d",
    &major,
    &minor,
    &patch
  ) == 3;
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

  if (
    !tachPhienBan(
      phienBanMoi,
      newMajor,
      newMinor,
      newPatch
    )
  ) {
    return false;
  }

  if (
    !tachPhienBan(
      phienBanHienTai,
      currentMajor,
      currentMinor,
      currentPatch
    )
  ) {
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
// Đọc phiên bản trên GitHub
// =====================================================

String docPhienBanTuServer() {
  NetworkClientSecure client;
  client.setInsecure();

  HTTPClient http;

  String url =
    String(VERSION_URL) +
    "?time=" +
    String(millis());

  Serial.print("Dang doc phien ban: ");
  Serial.println(url);

  if (!http.begin(client, url)) {
    return "";
  }

  http.setConnectTimeout(10000);
  http.setTimeout(10000);

  http.setFollowRedirects(
    HTTPC_STRICT_FOLLOW_REDIRECTS
  );

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.print("HTTP code: ");
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
// Cập nhật firmware
// =====================================================

void capNhatFirmware(const String& phienBanMoi) {
  Serial.println();
  Serial.println("BAT DAU CAP NHAT FIRMWARE");

  Serial.print("Tu phien ban: ");
  Serial.println(CURRENT_VERSION);

  Serial.print("Len phien ban: ");
  Serial.println(phienBanMoi);

  tatTatCaLed();

  NetworkClientSecure client;
  client.setInsecure();
  client.setTimeout(20000);

  httpUpdate.rebootOnUpdate(true);

  httpUpdate.setFollowRedirects(
    HTTPC_STRICT_FOLLOW_REDIRECTS
  );

  httpUpdate.onStart([]() {
    Serial.println("Dang tai firmware...");
  });

  httpUpdate.onProgress([](int current, int total) {
    if (total > 0) {
      int percent =
        static_cast<int>(
          (current * 100LL) / total
        );

      Serial.printf(
        "Tien trinh: %d%%\r",
        percent
      );
    }
  });

  httpUpdate.onEnd([]() {
    Serial.println();
    Serial.println("Cap nhat thanh cong.");
  });

  httpUpdate.onError([](int error) {
    Serial.println();

    Serial.print("Loi OTA: ");
    Serial.println(error);

    Serial.println(
      httpUpdate.getLastErrorString()
    );
  });

  String firmwareUrl =
    String(FIRMWARE_URL) +
    "?version=" +
    phienBanMoi;

  t_httpUpdate_return result =
    httpUpdate.update(
      client,
      firmwareUrl,
      CURRENT_VERSION
    );

  if (result == HTTP_UPDATE_FAILED) {
    Serial.println("Cap nhat that bai.");
  } else if (result == HTTP_UPDATE_NO_UPDATES) {
    Serial.println("Khong co ban cap nhat.");
  }
}

void kiemTraCapNhatOTA() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  Serial.println();
  Serial.println("----- KIEM TRA OTA -----");

  Serial.print("Phien ban ESP32: ");
  Serial.println(CURRENT_VERSION);

  String phienBanServer = docPhienBanTuServer();

  if (phienBanServer.length() == 0) {
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
    Serial.println(
      "ESP32 dang o phien ban moi nhat."
    );
  }
}

// =====================================================
// Setup và loop
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(CAM_BIEN_SANG, INPUT);
  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);

  for (int i = 0; i < SO_LED; i++) {
    pinMode(ledPins[i], OUTPUT);
  }

  tatTatCaLed();

  Serial.println();
  Serial.println("ESP32 bat dau khoi dong.");

  // Chỉ có tác dụng chuyển tiếp ở phiên bản 1.0.2
  chuyenWiFiHienTaiVaoNVS();

  if (!ketNoiWiFiDaLuu()) {
    batTrangCauHinhWiFi();
  }
}

void loop() {
  xuLyNutCauHinh();
  capNhatHieuUngLed();

  if (configPortalActive) {
    configServer.handleClient();
    delay(2);
    return;
  }

  duyTriWiFi();

  unsigned long hienTai = millis();

  if (
    WiFi.status() == WL_CONNECTED &&
    !daKiemTraOTALanDau &&
    hienTai >= OTA_CHECK_FIRST
  ) {
    daKiemTraOTALanDau = true;
    thoiGianKiemTraOTATruoc = hienTai;

    kiemTraCapNhatOTA();
  }

  if (
    WiFi.status() == WL_CONNECTED &&
    daKiemTraOTALanDau &&
    hienTai - thoiGianKiemTraOTATruoc >=
      OTA_CHECK_INTERVAL
  ) {
    thoiGianKiemTraOTATruoc = hienTai;

    kiemTraCapNhatOTA();
  }

  delay(1);
}
