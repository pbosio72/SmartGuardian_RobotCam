/*
 * ============================================================================
 * ESP32-CAM MONITOR WITH POSITIONAL SERVO CONTROL
 * ============================================================================
 * 
 * Description:
 * Monitor CAM using ESP32-CAM with positional servo motors for pan/tilt
 * control via web interface with customizable color theme.
 * 
 * Features:
 * - Live video streaming
 * - Pan/Tilt positional servo control with HOME button
 * - Real-time position display
 * - Flash LED with PWM brightness
 * - Photo capture and download
 * - Image orientation control
 * - Resolution adjustment
 * - FPS control
 * - Customizable color theme with HUE picker (saved to EEPROM)
 * - OLED display
 * - WiFi Access Point
 * 
 * Hardware:
 * - ESP32-CAM (AI-Thinker) with OV3660 camera
 * - 2x POSITIONAL servo motors (MG90S or similar)
 * - SSD1306 OLED display (128x64, I2C)
 * - Flash LED (GPIO 4)
 * - External 5V 2A power supply for servos
 * 
 * Pin Configuration:
 * - Pan Servo: GPIO 12
 * - Tilt Servo: GPIO 13
 * - OLED SDA: GPIO 14
 * - OLED SCL: GPIO 15
 * - Flash LED: GPIO 4
 * 
 * IMPORTANT:
 * Adjust PAN/TILT min/max angles to prevent cable tangling!
 * 
 * ============================================================================
 */

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <ArduinoOTA.h>

// ===========================================
// WIFI CREDENTIALS
// ===========================================
const char* ssid = "ESP32-CAM-Monitor";
const char* password = "12345678";

// ===========================================
// OTA (Over-The-Air) UPDATE CONFIGURATION
// ===========================================
const char* ota_hostname = "Monitor-CAM";      // Device name for OTA
const char* ota_password = "12345678";         // Password for OTA updates

// ===========================================
// SERVO CONFIGURATION - POSITIONAL
// ===========================================
#define PAN_SERVO_PIN     12
#define TILT_SERVO_PIN    13

// Servo angle limits (ADJUST to prevent cable tangling!)
#define PAN_MIN_ANGLE     0       // Left limit (full range)
#define PAN_MAX_ANGLE     180     // Right limit (full range)
#define PAN_CENTER_ANGLE  90      // Center position

#define TILT_MIN_ANGLE    0       // Down limit (full range)
#define TILT_MAX_ANGLE    180     // Up limit (full range)
#define TILT_CENTER_ANGLE 90      // Center position

// Movement step per button press (degrees)
#define PAN_STEP          2.5    // Reduced for smoother movement
#define TILT_STEP         2.5    // Reduced for smoother movement

// ===========================================
// CAMERA PINS (AI-Thinker ESP32-CAM)
// ===========================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define LED_GPIO_NUM       4

// ===========================================
// I2C AND OLED
// ===========================================
#define I2C_SDA 14
#define I2C_SCL 15
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ===========================================
// GLOBAL VARIABLES
// ===========================================
httpd_handle_t camera_httpd = NULL;
bool flashOn = false;
int flashBrightness = 0;

#define PWM_CHANNEL 7
#define PWM_FREQ 5000
#define PWM_RESOLUTION 8

bool verticalFlip = true;              // Start with 180° flip enabled
bool horizontalMirror = true;          // Start with 180° flip enabled
framesize_t currentResolution = FRAMESIZE_VGA;

Servo panServo;
Servo tiltServo;
int currentPanAngle = PAN_CENTER_ANGLE;
int currentTiltAngle = TILT_CENTER_ANGLE;

Preferences preferences;
int savedHue = 210; // Default blue

// Preset positions storage (3 presets = 6 values)
int preset1_pan = 90;
int preset1_tilt = 90;
int preset2_pan = 90;
int preset2_tilt = 90;
int preset3_pan = 90;
int preset3_tilt = 90;

// ===========================================
// OLED DISPLAY
// ===========================================
void updateDisplay(String line1, String line2 = "", String line3 = "", String line4 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  if (line2.length() > 0) {
    display.setCursor(0, 16);
    display.println(line2);
  }
  if (line3.length() > 0) {
    display.setCursor(0, 32);
    display.println(line3);
  }
  if (line4.length() > 0) {
    display.setCursor(0, 48);
    display.println(line4);
  }
  display.display();
}

// ===========================================
// CAMERA FUNCTIONS
// ===========================================
void applyOrientation() {
  sensor_t * s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_vflip(s, verticalFlip ? 1 : 0);
    s->set_hmirror(s, horizontalMirror ? 1 : 0);
  }
}

void applyResolution(framesize_t newRes) {
  sensor_t * s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_framesize(s, newRes);
    currentResolution = newRes;
  }
}

// ===========================================
// SERVO CONTROL - POSITIONAL
// ===========================================
void setPanAngle(int angle) {
  angle = constrain(angle, PAN_MIN_ANGLE, PAN_MAX_ANGLE);
  currentPanAngle = angle;
  panServo.write(angle);
}

void setTiltAngle(int angle) {
  angle = constrain(angle, TILT_MIN_ANGLE, TILT_MAX_ANGLE);
  currentTiltAngle = angle;
  tiltServo.write(angle);
}

void movePanLeft() {
  setPanAngle(currentPanAngle - PAN_STEP);
}

void movePanRight() {
  setPanAngle(currentPanAngle + PAN_STEP);
}

void moveTiltUp() {
  setTiltAngle(currentTiltAngle + TILT_STEP);
}

void moveTiltDown() {
  setTiltAngle(currentTiltAngle - TILT_STEP);
}

void moveToHome() {
  // Smooth gradual movement to home position
  int stepsPan = abs(currentPanAngle - PAN_CENTER_ANGLE) / 5;
  int stepsTilt = abs(currentTiltAngle - TILT_CENTER_ANGLE) / 5;
  int maxSteps = max(stepsPan, stepsTilt);
  
  if (maxSteps > 0) {
    for (int i = 0; i <= maxSteps; i++) {
      int newPan = map(i, 0, maxSteps, currentPanAngle, PAN_CENTER_ANGLE);
      int newTilt = map(i, 0, maxSteps, currentTiltAngle, TILT_CENTER_ANGLE);
      setPanAngle(newPan);
      setTiltAngle(newTilt);
      delay(50);  // Small delay for smooth movement
    }
  }
  
  // Final precise position
  setPanAngle(PAN_CENTER_ANGLE);
  setTiltAngle(TILT_CENTER_ANGLE);
}

// ===========================================
// HTTP HANDLERS - CAMERA
// ===========================================
static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

static esp_err_t photo_handler(httpd_req_t *req) {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  char filename[50];
  snprintf(filename, sizeof(filename), "attachment; filename=photo_%lu.jpg", millis());
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", filename);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

// ===========================================
// HTTP HANDLERS - FLASH
// ===========================================
static esp_err_t flash_on_handler(httpd_req_t *req) {
  flashBrightness = 255;
  ledcWrite(PWM_CHANNEL, flashBrightness);
  flashOn = true;
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

static esp_err_t flash_off_handler(httpd_req_t *req) {
  flashBrightness = 0;
  ledcWrite(PWM_CHANNEL, flashBrightness);
  flashOn = false;
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

static esp_err_t brightness_handler(httpd_req_t *req) {
  char buf[100];
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    char param[32];
    if (httpd_query_key_value(buf, "value", param, sizeof(param)) == ESP_OK) {
      flashBrightness = atoi(param);
      ledcWrite(PWM_CHANNEL, flashBrightness);
    }
  }
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

// ===========================================
// HTTP HANDLERS - ORIENTATION & RESOLUTION
// ===========================================
static esp_err_t rotate_handler(httpd_req_t *req) {
  char buf[100];
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    char param[32];
    if (httpd_query_key_value(buf, "mode", param, sizeof(param)) == ESP_OK) {
      if (strcmp(param, "normal") == 0) {
        verticalFlip = false;
        horizontalMirror = false;
      } else if (strcmp(param, "180") == 0) {
        verticalFlip = true;
        horizontalMirror = true;
      }
      applyOrientation();
    }
  }
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

static esp_err_t resolution_handler(httpd_req_t *req) {
  char buf[100];
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    char param[32];
    if (httpd_query_key_value(buf, "res", param, sizeof(param)) == ESP_OK) {
      framesize_t newRes = FRAMESIZE_VGA;
      if (strcmp(param, "qvga") == 0) newRes = FRAMESIZE_QVGA;
      else if (strcmp(param, "vga") == 0) newRes = FRAMESIZE_VGA;
      else if (strcmp(param, "svga") == 0) newRes = FRAMESIZE_SVGA;
      else if (strcmp(param, "xga") == 0) newRes = FRAMESIZE_XGA;
      else if (strcmp(param, "sxga") == 0) newRes = FRAMESIZE_SXGA;
      else if (strcmp(param, "uxga") == 0) newRes = FRAMESIZE_UXGA;
      applyResolution(newRes);
    }
  }
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

// ===========================================
// HTTP HANDLERS - SERVO SLIDERS
// ===========================================
static esp_err_t servo_home_handler(httpd_req_t *req) {
  moveToHome();
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

static esp_err_t servo_pan_handler(httpd_req_t *req) {
  char buf[100];
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    char param[32];
    if (httpd_query_key_value(buf, "value", param, sizeof(param)) == ESP_OK) {
      int angle = atoi(param);
      setPanAngle(angle);
    }
  }
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

static esp_err_t servo_tilt_handler(httpd_req_t *req) {
  char buf[100];
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    char param[32];
    if (httpd_query_key_value(buf, "value", param, sizeof(param)) == ESP_OK) {
      int angle = atoi(param);
      setTiltAngle(angle);
    }
  }
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

// ===========================================
// HTTP HANDLERS - PRESET POSITIONS
// ===========================================
static esp_err_t preset_save_handler(httpd_req_t *req) {
  char buf[100];
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    char slot_param[32];
    char pan_param[32];
    char tilt_param[32];
    
    if (httpd_query_key_value(buf, "slot", slot_param, sizeof(slot_param)) == ESP_OK &&
        httpd_query_key_value(buf, "pan", pan_param, sizeof(pan_param)) == ESP_OK &&
        httpd_query_key_value(buf, "tilt", tilt_param, sizeof(tilt_param)) == ESP_OK) {
      
      int slot = atoi(slot_param);
      int pan = atoi(pan_param);
      int tilt = atoi(tilt_param);
      
      preferences.begin("monitor", false);
      
      if (slot == 1) {
        preset1_pan = pan;
        preset1_tilt = tilt;
        preferences.putInt("p1_pan", pan);
        preferences.putInt("p1_tilt", tilt);
      } else if (slot == 2) {
        preset2_pan = pan;
        preset2_tilt = tilt;
        preferences.putInt("p2_pan", pan);
        preferences.putInt("p2_tilt", tilt);
      } else if (slot == 3) {
        preset3_pan = pan;
        preset3_tilt = tilt;
        preferences.putInt("p3_pan", pan);
        preferences.putInt("p3_tilt", tilt);
      }
      
      preferences.end();
    }
  }
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

static esp_err_t preset_recall_handler(httpd_req_t *req) {
  char buf[100];
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    char slot_param[32];
    
    if (httpd_query_key_value(buf, "slot", slot_param, sizeof(slot_param)) == ESP_OK) {
      int slot = atoi(slot_param);
      int target_pan = 90;
      int target_tilt = 90;
      
      if (slot == 1) {
        target_pan = preset1_pan;
        target_tilt = preset1_tilt;
      } else if (slot == 2) {
        target_pan = preset2_pan;
        target_tilt = preset2_tilt;
      } else if (slot == 3) {
        target_pan = preset3_pan;
        target_tilt = preset3_tilt;
      }
      
      setPanAngle(target_pan);
      setTiltAngle(target_tilt);
    }
  }
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

static esp_err_t preset_reset_handler(httpd_req_t *req) {
  // Reset all presets to default 90/90
  preset1_pan = 90;
  preset1_tilt = 90;
  preset2_pan = 90;
  preset2_tilt = 90;
  preset3_pan = 90;
  preset3_tilt = 90;
  
  preferences.begin("monitor", false);
  preferences.putInt("p1_pan", 90);
  preferences.putInt("p1_tilt", 90);
  preferences.putInt("p2_pan", 90);
  preferences.putInt("p2_tilt", 90);
  preferences.putInt("p3_pan", 90);
  preferences.putInt("p3_tilt", 90);
  preferences.end();
  
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

// ===========================================
// HTTP HANDLERS - COLOR HUE
// ===========================================
static esp_err_t hue_get_handler(httpd_req_t *req) {
  char json[50];
  snprintf(json, sizeof(json), "{\"hue\":%d}", savedHue);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, json, strlen(json));
  return ESP_OK;
}

static esp_err_t hue_set_handler(httpd_req_t *req) {
  char buf[100];
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    char param[32];
    if (httpd_query_key_value(buf, "value", param, sizeof(param)) == ESP_OK) {
      savedHue = atoi(param);
      preferences.begin("monitor", false);
      preferences.putInt("hue", savedHue);
      preferences.end();
    }
  }
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

// ===========================================
// HTTP HANDLER - WEB PAGE
// ===========================================
static esp_err_t index_handler(httpd_req_t *req) {
  const char* html = 
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title> Monitor CAM </title>"
    "<meta name='theme-color' content='#1e3c72'>"
    "<meta name='apple-mobile-web-app-capable' content='yes'>"
    "<meta name='apple-mobile-web-app-status-bar-style' content='black-translucent'>"
    "<link rel='manifest' href='data:application/json;base64,eyJuYW1lIjoiQmFieSBNb25pdG9yIiwic2hvcnRfbmFtZSI6IkJhYnlDYW0iLCJzdGFydF91cmwiOiIvIiwiZGlzcGxheSI6InN0YW5kYWxvbmUiLCJiYWNrZ3JvdW5kX2NvbG9yIjoiIzFlM2M3MiIsInRoZW1lX2NvbG9yIjoiIzFlM2M3MiIsImljb25zIjpbeyJzcmMiOiJkYXRhOmltYWdlL3BuZztiYXNlNjQsaVZCT1J3MEtHZ29BQUFBTlNVaEVVZ0FBQUdBQUFBQmdDQVlBQUFEOGZtaGdBQUFBUmtsRVFWUjRYdTJZMFU0Q1FSQ0Z3Ui9mNHY4Z0g5L2YvLy9mLy8vLy85L2YvLy85OUE9PSIsInNpemVzIjoiOTZ4OTYiLCJ0eXBlIjoiaW1hZ2UvcG5nIn1dfQ=='>"
    "<style>"
    "body{font-family:'Segoe UI',sans-serif;text-align:center;margin:0;background:linear-gradient(135deg,#1e3c72 0%,#2a5298 100%);color:#fff;padding:5px;min-height:100vh;transition:background 0.5s}"
    "h1{margin:10px;font-size:22px;text-shadow:2px 2px 4px rgba(0,0,0,0.3);font-weight:300}"
    "#imageContainer{width:100%;max-width:640px;margin:8px auto;border:2px solid rgba(100,200,255,0.4);border-radius:10px;overflow:hidden;background:#000;box-shadow:0 8px 32px rgba(0,0,0,0.4);transition:border-color 0.5s}"
    "img{width:100%;display:block;min-height:150px}"
    "button{background:linear-gradient(135deg,#00d4ff 0%,#0099cc 100%);color:#fff;border:none;padding:5px 12px;font-size:11px;margin:3px;cursor:pointer;border-radius:15px;min-width:70px;font-weight:600;transition:all 0.3s;box-shadow:0 2px 8px rgba(0,0,0,0.3)}"
    "button:hover{transform:translateY(-1px);box-shadow:0 4px 12px rgba(0,212,255,0.4)}"
    ".off{background:linear-gradient(135deg,#ff6b6b 0%,#ee5a6f 100%)}"
    ".photo{background:linear-gradient(135deg,#4facfe 0%,#00f2fe 100%)}"
    ".rotate{background:linear-gradient(135deg,#43e97b 0%,#38f9d7 100%);transition:background 0.5s}"
    ".resolution{background:linear-gradient(135deg,#a8edea 0%,#fed6e3 100%);color:#1e3c72}"
    ".servo{background:linear-gradient(135deg,#00d4ff 0%,#0099cc 100%);font-size:14px;padding:10px 15px;transition:background 0.5s}"
    ".home{background:linear-gradient(135deg,#ffd89b 0%,#19547b 100%);font-size:12px;font-weight:700;transition:background 0.5s}"
    ".preset-save{background:linear-gradient(135deg,#a8edea 0%,#fed6e3 100%);color:#1e3c72;transition:background 0.5s}"
    ".preset-recall{background:linear-gradient(135deg,#43e97b 0%,#38f9d7 100%);transition:background 0.5s}"
    ".slider-container{margin:8px auto;max-width:300px}"
    "input[type=range]{width:100%;height:6px;-webkit-appearance:none;background:rgba(0,212,255,0.2);outline:none;border-radius:8px;transition:background 0.5s}"
    "input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;background:#00d4ff;cursor:pointer;border-radius:50%;box-shadow:0 2px 6px rgba(0,212,255,0.5)}"
    "input[type=range]::-moz-range-thumb{width:18px;height:18px;background:#00d4ff;cursor:pointer;border-radius:50%;border:none}"
    ".section{margin:8px auto;padding:8px;max-width:640px;background:rgba(100,200,255,0.15);backdrop-filter:blur(10px);border-radius:12px;border:1px solid rgba(100,200,255,0.3);box-shadow:0 4px 15px rgba(0,0,0,0.2);transition:all 0.5s}"
    ".section h3{color:#fff;margin:5px 0 8px 0;font-weight:400;font-size:13px}"
    ".compact-row{display:flex;justify-content:center;flex-wrap:wrap;gap:3px}"
    "#colorPicker{background:linear-gradient(to right,hsl(0,100%,50%),hsl(60,100%,50%),hsl(120,100%,50%),hsl(180,100%,50%),hsl(240,100%,50%),hsl(300,100%,50%),hsl(360,100%,50%));height:20px;border:2px solid rgba(255,255,255,0.3)}"
    "#colorPicker::-webkit-slider-thumb{width:24px;height:24px;border:2px solid white}"
    "</style></head><body>"
    "<h1>Monitor CAM</h1>"
    "<div id='imageContainer'><img id='stream' src='/capture' ondblclick='toggleFullscreen()'></div>"
    "<div class='section'><h3>Camera Position</h3>"
    "<div class='slider-container'>"
    "<label style='font-size:11px'>Pan (Horizontal):</label>"
    "<input type='range' min='0' max='180' value='90' id='panSlider' oninput='setPan(this.value)'>"
    "</div>"
    "<div class='slider-container'>"
    "<label style='font-size:11px'>Tilt (Vertical):</label>"
    "<input type='range' min='0' max='180' value='90' id='tiltSlider' oninput='setTilt(this.value)'>"
    "</div>"
    "<div class='compact-row' style='margin-top:8px'>"
    "<button class='preset-recall' onclick='recallPreset(1)' style='font-size:10px;min-width:60px;padding:4px 8px'>POS 1</button>"
    "<button class='preset-recall' onclick='recallPreset(2)' style='font-size:10px;min-width:60px;padding:4px 8px'>POS 2</button>"
    "<button class='preset-recall' onclick='recallPreset(3)' style='font-size:10px;min-width:60px;padding:4px 8px'>POS 3</button>"
    "</div>"
    "<div style='margin-top:5px;text-align:center'>"
    "<button class='home' onclick='goHome()' style='margin:3px;min-width:80px'>HOME</button><br>"
    "<button class='off' onclick='resetPresets()' style='margin:3px;font-size:10px;min-width:80px;padding:4px 8px'>RESET</button>"
    "</div>"
    "<div class='compact-row' style='margin-top:5px'>"
    "<button class='preset-save' onclick='savePreset(1)' style='font-size:10px;min-width:60px;padding:4px 8px'>SAVE 1</button>"
    "<button class='preset-save' onclick='savePreset(2)' style='font-size:10px;min-width:60px;padding:4px 8px'>SAVE 2</button>"
    "<button class='preset-save' onclick='savePreset(3)' style='font-size:10px;min-width:60px;padding:4px 8px'>SAVE 3</button>"
    "</div>"
    "</div>"
    "<div class='section'><h3>Flash</h3><div class='compact-row'>"
    "<button onclick='flashOn()'>ON</button><button class='off' onclick='flashOff()'>OFF</button><button class='photo' onclick='takePhoto()'>PHOTO</button></div>"
    "<div class='slider-container'><input type='range' min='0' max='255' value='0' id='brightness' oninput='setBrightness(this.value)'>"
    "<span id='brightnessValue' style='font-size:14px;font-weight:600'>0</span></div></div>"
    "<div class='section'><h3>Settings</h3><div class='slider-container'><label style='font-size:11px'>FPS:</label>"
    "<input type='range' min='100' max='2000' value='150' id='fpsSlider' oninput='setFPS(this.value)' step='50'>"
    "<span id='fpsValue' style='font-size:14px;font-weight:600'>6.7 fps</span></div>"
    "<div class='compact-row' style='margin-top:8px'><button class='rotate' onclick='setRotation(\"normal\")'>NORMAL</button>"
    "<button class='rotate' onclick='setRotation(\"180\")'>FLIP 180</button></div></div>"
    "<div class='section'><h3>Resolution</h3><div class='compact-row'>"
    "<button class='resolution' onclick='setResolution(\"vga\")'>VGA</button>"
    "<button class='resolution' onclick='setResolution(\"svga\")'>SVGA</button>"
    "<button class='resolution' onclick='setResolution(\"xga\")'>XGA</button></div></div>"
    "<div style='background:rgba(0,0,0,0.3);padding:8px;border-radius:8px;margin:8px auto;max-width:640px;font-size:11px'>"
    "<span style='color:rgba(255,255,255,0.7)'>Res:</span> <span id='currentRes' style='color:#4facfe;font-weight:600'>VGA</span> | "
    "<span style='color:rgba(255,255,255,0.7)'>FPS:</span> <span id='currentFps' style='color:#4facfe;font-weight:600'>6.7</span></div>"
    "<div class='section' style='background:rgba(0,0,0,0.4);border:2px solid rgba(255,255,255,0.3);margin-top:15px'><h3>Color Theme</h3>"
    "<div class='slider-container'><label style='font-size:11px'>Choose Color (HUE):</label>"
    "<input type='range' min='0' max='360' value='210' id='colorPicker' oninput='updateThemeColor(this.value)'>"
    "<div style='margin-top:5px;font-size:10px'><span style='color:rgba(255,255,255,0.7)'>HUE:</span> "
    "<span id='hueValue' style='color:#4facfe;font-weight:600'>210&deg;</span><span style='margin-left:10px'>|</span>"
    "<span style='margin-left:10px'><span id='colorPreview' style='display:inline-block;width:20px;height:20px;border-radius:50%;vertical-align:middle;border:2px solid white'></span></span></div>"
    "<div style='margin-top:8px;font-size:9px;color:rgba(255,255,255,0.5)'>Preset: "
    "<button onclick='updateThemeColor(210)' style='min-width:40px;padding:3px 8px;font-size:9px;background:#1e3c72'>Blue</button>"
    "<button onclick='updateThemeColor(340)' style='min-width:40px;padding:3px 8px;font-size:9px;background:#ee0979'>Pink</button>"
    "<button onclick='updateThemeColor(120)' style='min-width:40px;padding:3px 8px;font-size:9px;background:#56ab2f'>Green</button>"
    "<button onclick='updateThemeColor(30)' style='min-width:40px;padding:3px 8px;font-size:9px;background:#ff6a00'>Orange</button>"
    "<button onclick='updateThemeColor(280)' style='min-width:40px;padding:3px 8px;font-size:9px;background:#9b59b6'>Purple</button>"
    "</div></div></div>"
    "<script>"
    "let img=document.getElementById('stream');let refreshInterval;let currentRefreshRate=150;"
    "function updateImage(){img.src='/capture?t='+new Date().getTime()}"
    "function flashOn(){fetch('/flash/on')}"
    "function flashOff(){fetch('/flash/off');document.getElementById('brightness').value=0;document.getElementById('brightnessValue').textContent='0'}"
    "function setBrightness(val){fetch('/brightness?value='+val);document.getElementById('brightnessValue').textContent=val}"
    "function setRotation(mode){fetch('/rotate?mode='+mode)}"
    "function setResolution(res){fetch('/resolution?res='+res);document.getElementById('currentRes').textContent=res.toUpperCase()}"
    "function setFPS(val){currentRefreshRate=2100-parseInt(val);let fps=(1000/currentRefreshRate).toFixed(1);document.getElementById('fpsValue').textContent=fps+' fps';document.getElementById('currentFps').textContent=fps;clearInterval(refreshInterval);refreshInterval=setInterval(updateImage,currentRefreshRate)}"
    "function takePhoto(){window.open('/photo','_blank')}"
    "function setPan(angle){fetch('/servo/pan?value='+angle)}"
    "function setTilt(angle){fetch('/servo/tilt?value='+angle)}"
    "function goHome(){document.getElementById('panSlider').value=90;document.getElementById('tiltSlider').value=90;setPan(90);setTilt(90)}"
    "function savePreset(slot){let pan=document.getElementById('panSlider').value;let tilt=document.getElementById('tiltSlider').value;fetch('/preset/save?slot='+slot+'&pan='+pan+'&tilt='+tilt).then(()=>alert('Position '+slot+' saved!'))}"
    "function recallPreset(slot){fetch('/preset/recall?slot='+slot)}"
    "function resetPresets(){if(confirm('Reset all preset positions to default (90°/90°)?')){fetch('/preset/reset').then(()=>alert('All presets reset to default!'))}}"
    "function updateThemeColor(hue){hue=parseInt(hue);document.getElementById('hueValue').textContent=hue+'°';document.getElementById('colorPicker').value=hue;"
    "fetch('/hue/set?value='+hue);const compHue=(hue+180)%360;const hue1=(hue+30)%360;"
    "const mainLight='hsl('+hue+',60%,60%)';const mainDark='hsl('+hue+',80%,40%)';const bgColor1='hsl('+hue+',50%,30%)';const bgColor2='hsl('+hue1+',50%,35%)';"
    "document.getElementById('colorPreview').style.background='hsl('+hue+',70%,50%)';"
    "document.body.style.background='linear-gradient(135deg,'+bgColor1+' 0%,'+bgColor2+' 100%)';"
    "document.getElementById('imageContainer').style.borderColor='hsla('+hue+',70%,60%,0.5)';"
    "document.querySelectorAll('.section').forEach(s=>{s.style.background='hsla('+hue+',60%,50%,0.15)';s.style.borderColor='hsla('+hue+',60%,50%,0.3)'});"
    "document.querySelectorAll('.servo').forEach(b=>b.style.background='linear-gradient(135deg,'+mainLight+' 0%,'+mainDark+' 100%)');"
    "const home=document.querySelector('.home');if(home)home.style.background='linear-gradient(135deg,hsl('+compHue+',70%,60%) 0%,hsl('+compHue+',70%,45%) 100%)';"
    "document.querySelectorAll('.rotate').forEach(b=>b.style.background='linear-gradient(135deg,hsl('+compHue+',60%,55%) 0%,hsl('+compHue+',60%,45%) 100%)');"
    "document.querySelectorAll('input[type=range]').forEach(s=>{if(s.id!=='colorPicker')s.style.background='hsla('+hue+',70%,50%,0.2)'})}"
    "function toggleFullscreen(){const elem=document.getElementById('imageContainer');if(!document.fullscreenElement){elem.requestFullscreen().catch(err=>console.log(err))}else{document.exitFullscreen()}}"
    "if('serviceWorker'in navigator){window.addEventListener('load',()=>{navigator.serviceWorker.register('data:text/javascript;base64,'+btoa('self.addEventListener(\"fetch\",e=>{e.respondWith(fetch(e.request))})')).catch(()=>{})})}"
    "refreshInterval=setInterval(updateImage,currentRefreshRate);"
    "fetch('/hue/get').then(r=>r.json()).then(data=>updateThemeColor(data.hue));"
    "</script></body></html>";
  httpd_resp_send(req, html, strlen(html));
  return ESP_OK;
}

// ===========================================
// WEB SERVER
// ===========================================
void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_open_sockets = 13;
  config.max_uri_handlers = 16;  // Increased for preset save/recall/reset
  config.ctrl_port = 32768;

  httpd_uri_t index_uri = {.uri="/", .method=HTTP_GET, .handler=index_handler, .user_ctx=NULL};
  httpd_uri_t capture_uri = {.uri="/capture", .method=HTTP_GET, .handler=capture_handler, .user_ctx=NULL};
  httpd_uri_t photo_uri = {.uri="/photo", .method=HTTP_GET, .handler=photo_handler, .user_ctx=NULL};
  httpd_uri_t flash_on_uri = {.uri="/flash/on", .method=HTTP_GET, .handler=flash_on_handler, .user_ctx=NULL};
  httpd_uri_t flash_off_uri = {.uri="/flash/off", .method=HTTP_GET, .handler=flash_off_handler, .user_ctx=NULL};
  httpd_uri_t brightness_uri = {.uri="/brightness", .method=HTTP_GET, .handler=brightness_handler, .user_ctx=NULL};
  httpd_uri_t rotate_uri = {.uri="/rotate", .method=HTTP_GET, .handler=rotate_handler, .user_ctx=NULL};
  httpd_uri_t resolution_uri = {.uri="/resolution", .method=HTTP_GET, .handler=resolution_handler, .user_ctx=NULL};
  httpd_uri_t servo_pan_uri = {.uri="/servo/pan", .method=HTTP_GET, .handler=servo_pan_handler, .user_ctx=NULL};
  httpd_uri_t servo_tilt_uri = {.uri="/servo/tilt", .method=HTTP_GET, .handler=servo_tilt_handler, .user_ctx=NULL};
  httpd_uri_t servo_home_uri = {.uri="/servo/home", .method=HTTP_GET, .handler=servo_home_handler, .user_ctx=NULL};
  httpd_uri_t preset_save_uri = {.uri="/preset/save", .method=HTTP_GET, .handler=preset_save_handler, .user_ctx=NULL};
  httpd_uri_t preset_recall_uri = {.uri="/preset/recall", .method=HTTP_GET, .handler=preset_recall_handler, .user_ctx=NULL};
  httpd_uri_t preset_reset_uri = {.uri="/preset/reset", .method=HTTP_GET, .handler=preset_reset_handler, .user_ctx=NULL};
  httpd_uri_t hue_get_uri = {.uri="/hue/get", .method=HTTP_GET, .handler=hue_get_handler, .user_ctx=NULL};
  httpd_uri_t hue_set_uri = {.uri="/hue/set", .method=HTTP_GET, .handler=hue_set_handler, .user_ctx=NULL};

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &photo_uri);
    httpd_register_uri_handler(camera_httpd, &flash_on_uri);
    httpd_register_uri_handler(camera_httpd, &flash_off_uri);
    httpd_register_uri_handler(camera_httpd, &brightness_uri);
    httpd_register_uri_handler(camera_httpd, &rotate_uri);
    httpd_register_uri_handler(camera_httpd, &resolution_uri);
    httpd_register_uri_handler(camera_httpd, &servo_pan_uri);
    httpd_register_uri_handler(camera_httpd, &servo_tilt_uri);
    httpd_register_uri_handler(camera_httpd, &servo_home_uri);
    httpd_register_uri_handler(camera_httpd, &preset_save_uri);
    httpd_register_uri_handler(camera_httpd, &preset_recall_uri);
    httpd_register_uri_handler(camera_httpd, &preset_reset_uri);
    httpd_register_uri_handler(camera_httpd, &hue_get_uri);
    httpd_register_uri_handler(camera_httpd, &hue_set_uri);
  }
}

// ===========================================
// SETUP
// ===========================================
void setup() {
  // Initialize serial FIRST - before anything else
  Serial.begin(115200);
  delay(1000);  // Give serial time to initialize
  Serial.println("\n\n");
  Serial.println("======================");
  Serial.println("ESP32-CAM STARTING");
  Serial.println("======================");
  Serial.println("Serial OK!");
  
  Serial.println("Initializing I2C...");
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("I2C OK!");
  
  Serial.println("Initializing OLED...");
  if(display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED OK!");
    updateDisplay("ESP32-CAM", "Monitor CAM", "Initializing...");
  } else {
    Serial.println("OLED FAILED!");
  }

  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(LED_GPIO_NUM, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);

  // Load saved HUE from EEPROM
  preferences.begin("monitor", false);
  savedHue = preferences.getInt("hue", 210);
  
  // Load preset positions from EEPROM
  preset1_pan = preferences.getInt("p1_pan", 90);
  preset1_tilt = preferences.getInt("p1_tilt", 90);
  preset2_pan = preferences.getInt("p2_pan", 90);
  preset2_tilt = preferences.getInt("p2_tilt", 90);
  preset3_pan = preferences.getInt("p3_pan", 90);
  preset3_tilt = preferences.getInt("p3_tilt", 90);
  
  preferences.end();
  Serial.println("Preferences loaded from EEPROM");

  // Initialize positional servos
  panServo.attach(PAN_SERVO_PIN);
  tiltServo.attach(TILT_SERVO_PIN);
  moveToHome();
  delay(500);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (esp_camera_init(&config) != ESP_OK) {
    updateDisplay("ERROR!", "Camera init", "failed!");
    return;
  }

  applyOrientation();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  
  Serial.println("WiFi AP started");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("IP Address: ");
  Serial.println(IP);
  
  // Configure OTA (Over-The-Air updates)
  Serial.println("Configuring OTA...");
  ArduinoOTA.setHostname(ota_hostname);
  Serial.print("OTA Hostname: ");
  Serial.println(ota_hostname);
  
  ArduinoOTA.setPassword(ota_password);
  Serial.println("OTA Password set");
  
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    Serial.println("OTA Update Start - Type: " + type);
    updateDisplay("OTA Update", "Type: " + type, "Updating...");
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update Complete!");
    updateDisplay("OTA Update", "Complete!", "Rebooting...");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percent = (progress / (total / 100));
    Serial.printf("OTA Progress: %u%%\r", percent);
    updateDisplay("OTA Update", "Progress:", String(percent) + "%");
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    String errorMsg = "Error: ";
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
      errorMsg += "Auth Failed";
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
      errorMsg += "Begin Failed";
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
      errorMsg += "Connect Failed";
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
      errorMsg += "Receive Failed";
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
      errorMsg += "End Failed";
    }
    updateDisplay("OTA Update", errorMsg, "Reboot device");
  });
  
  ArduinoOTA.begin();
  Serial.println("OTA Ready!");
  Serial.print("OTA can be accessed at: ");
  Serial.println(ota_hostname);
  
  updateDisplay("WiFi: " + String(ssid), 
                "Pass: " + String(password),
                "IP: " + IP.toString(),
                "OTA: " + String(ota_hostname));
  
  Serial.println("Starting camera server...");
  startCameraServer();
  Serial.println("Setup complete!");
}

// ===========================================
// LOOP
// ===========================================
void loop() {
  ArduinoOTA.handle();  // Handle OTA updates
  delay(100);
}
