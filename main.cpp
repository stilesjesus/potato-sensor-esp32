#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <ArduinoJson.h>    // For JSON serialization
#include <time.h>           // Needed for NTP/time functions

// ──────────────────────────────────────────────────────────────────────────────
// USER CONFIGURATION: Change these to match your Wi-Fi SSID/password.
// ──────────────────────────────────────────────────────────────────────────────
const char* ssid     = "ssid";
const char* password = "password";

// ──────────────────────────────────────────────────────────────────────────────
// 1) Pin Definitions (ESP32 DevKit ↔ Waveshare 1.5" SSD1351)
// ──────────────────────────────────────────────────────────────────────────────
static const uint8_t OLED_MOSI = 23;  // VSPI MOSI
static const uint8_t OLED_SCLK = 18;  // VSPI SCLK
static const uint8_t OLED_DC   = 16;  // Data/Command pin
static const uint8_t OLED_RST  = 17;  // Reset pin
static const uint8_t OLED_CS   = 5;   // Chip Select pin

// ──────────────────────────────────────────────────────────────────────────────
// 2) Display Resolution
// ──────────────────────────────────────────────────────────────────────────────
static const int SCREEN_WIDTH  = 128;
static const int SCREEN_HEIGHT = 128;

// ──────────────────────────────────────────────────────────────────────────────
// 3) Color Definitions (16-bit 5-6-5 RGB)
// ──────────────────────────────────────────────────────────────────────────────
static const uint16_t COLOR_BLACK = 0x0000;
static const uint16_t COLOR_RED   = 0xF800;
static const uint16_t COLOR_BLUE  = 0x001F;

// ──────────────────────────────────────────────────────────────────────────────
// 4) Instantiate Adafruit_SSD1351
// ──────────────────────────────────────────────────────────────────────────────
Adafruit_SSD1351 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_CS, OLED_DC, OLED_RST);

// ──────────────────────────────────────────────────────────────────────────────
// 5) DHT22 on GPIO 22
// ──────────────────────────────────────────────────────────────────────────────
#define DHTPIN   22
#define DHTTYPE  DHT22
DHT_Unified dht(DHTPIN, DHTTYPE);

// ──────────────────────────────────────────────────────────────────────────────
// 6) Text metrics (textSize = 2 ⇒ each char = 12×16 px)
// ──────────────────────────────────────────────────────────────────────────────
static const int TEXT_SIZE   = 2;
static const int CHAR_WIDTH  = 12;
static const int CHAR_HEIGHT = 16;

// ──────────────────────────────────────────────────────────────────────────────
// 7) Four lines of text for the OLED. Must be declared before using snprintf().
// ──────────────────────────────────────────────────────────────────────────────
char lineStr[4][16];  // lineStr[0] = “TEMP: XXF”, lineStr[1] = “L:XX H:YY”, etc.

// ──────────────────────────────────────────────────────────────────────────────
// 8) Globals for sensor values + min/max tracking + last update timestamp
// ──────────────────────────────────────────────────────────────────────────────
float tMin =  1e6, tMax = -1e6;
float hMin =  1e6, hMax = -1e6;
float currentTempF = NAN, currentHum = NAN;
uint32_t lastReadTime   = 0;    // millis() of last DHT read
uint32_t lastUpdateTime = 0;    // UNIX timestamp from NTP

// ──────────────────────────────────────────────────────────────────────────────
// 9) Globals for burn-in phase tracking
// ──────────────────────────────────────────────────────────────────────────────
int lastPhase = -1;

// ──────────────────────────────────────────────────────────────────────────────
// 10) WebServer on port 80
// ──────────────────────────────────────────────────────────────────────────────
WebServer server(80);

// ──────────────────────────────────────────────────────────────────────────────
// Forward declarations
// ──────────────────────────────────────────────────────────────────────────────
void handleRoot();
void handleSensorData();
void drawReadings(int16_t offsetX, int16_t offsetY);
String getHtmlPage();

void setup() {
  // — Serial for debugging
  Serial.begin(115200);
  delay(200);

  // — Initialize DHT22
  dht.begin();

  // — Initialize SPI for the OLED (SSD1351 uses VSPI MOSI/SCLK; MISO isn’t used)
  SPI.begin(
    OLED_SCLK,   // SCLK = GPIO 18
    /* MISO */ -1,
    OLED_MOSI,   // MOSI = GPIO 23
    OLED_CS      // CS   = GPIO  5
  );

  // — Hardware reset pulse on the OLED’s RST pin
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(10);
  digitalWrite(OLED_RST, HIGH);
  delay(10);

  // — Initialize the SSD1351 display (8 MHz SPI, 16-bit color)
  oled.begin();

  // ──────────────── Rotate 90° clockwise ────────────────
  oled.setRotation(3);
  // ────────────────────────────────────────────────────────

  // — Draw initial placeholders (“--F” etc.) so you see them only briefly
  snprintf(lineStr[0], sizeof(lineStr[0]), "TEMP: --F");
  snprintf(lineStr[1], sizeof(lineStr[1]), "L:-- H:--");
  snprintf(lineStr[2], sizeof(lineStr[2]), "HUMID: --%%");
  snprintf(lineStr[3], sizeof(lineStr[3]), "L:-- H:--");
  drawReadings(0, 0);
  lastPhase = (int)((millis() / 60000UL) % 4);

  // — Connect to Wi-Fi
  Serial.printf("Connecting to Wi-Fi SSID \"%s\" …\n", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Wi-Fi connected. IP = ");
  Serial.println(WiFi.localIP());

  // ────────────────────────────────────────────────────────────────────────────
  // Set up NTP time synchronization (UTC, no DST).
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print("Waiting for NTP time sync");
  time_t now;
  while ((now = time(nullptr)) < 100000) {
    // time(nullptr) will be < 100000 until NTP replies. (Zero on boot.)
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.printf("NTP synced, current UNIX time = %lu\n", now);

  // ────────────────────────────────────────────────────────────────────────────
  // Set up HTTP handlers
  server.on("/",            handleRoot);
  server.on("/sensor-data", handleSensorData);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // — Handle any pending HTTP requests
  server.handleClient();

  // — Read DHT22 every 2 seconds
  if (millis() - lastReadTime >= 2000UL) {
    lastReadTime = millis();

    sensors_event_t event;
    // a) Read temperature in °C → convert to °F
    dht.temperature().getEvent(&event);
    if (!isnan(event.temperature)) {
      currentTempF = event.temperature * 9.0 / 5.0 + 32.0;
      tMin = min(tMin, currentTempF);
      tMax = max(tMax, currentTempF);
    } else {
      Serial.println(F("Error reading temperature!"));
    }

    // b) Read humidity
    dht.humidity().getEvent(&event);
    if (!isnan(event.relative_humidity)) {
      currentHum = event.relative_humidity;
      hMin = min(hMin, currentHum);
      hMax = max(hMax, currentHum);
    } else {
      Serial.println(F("Error reading humidity!"));
    }

    // c) Update lineStr[] so the OLED shows real data instead of “--F”
    {
      // “TEMP: XXF”
      int tf = (int)round(currentTempF);
      snprintf(lineStr[0], sizeof(lineStr[0]), "TEMP: %dF", tf);

      // “L:XX H:YY”
      int tmin_i = (int)round(tMin);
      int tmax_i = (int)round(tMax);
      snprintf(lineStr[1], sizeof(lineStr[1]), "L:%d H:%d", tmin_i, tmax_i);

      // “HUMID: ZZ%”
      int h_i = (int)round(currentHum);
      snprintf(lineStr[2], sizeof(lineStr[2]), "HUMID: %d%%", h_i);

      // “L:AA H:BB”
      int hmin_i = (int)round(hMin);
      int hmax_i = (int)round(hMax);
      snprintf(lineStr[3], sizeof(lineStr[3]), "L:%d H:%d", hmin_i, hmax_i);
    }

    // d) Record “lastUpdateTime” from NTP
    lastUpdateTime = (uint32_t) time(nullptr);
  }

  // — Jiggle the OLED contents once per minute to prevent burn-in
  {
    int phase = (int)((millis() / 60000UL) % 4);
    if (phase != lastPhase) {
      lastPhase = phase;
      int16_t offsetX = 0, offsetY = 0;
      switch (phase) {
        case 0: offsetX =  1; offsetY =  0; break; // shift right 1px
        case 1: offsetX =  1; offsetY = -1; break; // shift up    1px
        case 2: offsetX =  0; offsetY = -1; break; // shift left  1px
        case 3: offsetX =  0; offsetY =  0; break; // shift down  1px
      }
      drawReadings(offsetX, offsetY);
    }
  }
  // No extra delay—server.handleClient() and DHT logic are nonblocking.
}

// ──────────────────────────────────────────────────────────────────────────────
// 1) handleRoot() → serve the updated HTML page
// ──────────────────────────────────────────────────────────────────────────────
void handleRoot() {
  String html = getHtmlPage();
  server.send(200, "text/html", html);
}

// ──────────────────────────────────────────────────────────────────────────────
// 2) handleSensorData() → return JSON fields for the webpage’s fetch()
// ──────────────────────────────────────────────────────────────────────────────
void handleSensorData() {
  StaticJsonDocument<256> doc;
  doc["temperature"]  = isnan(currentTempF) ? -999.0 : currentTempF;
  doc["humidity"]     = isnan(currentHum)     ? -1.0   : currentHum;
  doc["temp_low"]     = isnan(tMin)           ? -999.0 : tMin;
  doc["temp_high"]    = isnan(tMax)           ? -999.0 : tMax;
  doc["hum_low"]      = isnan(hMin)           ? -1.0   : hMin;
  doc["hum_high"]     = isnan(hMax)           ? -1.0   : hMax;
  doc["last_updated"] = lastUpdateTime;
  String payload;
  serializeJson(doc, payload);
  server.send(200, "application/json", payload);
}

// ──────────────────────────────────────────────────────────────────────────────
// 3) drawReadings(offsetX, offsetY) → clear + print the four lines from lineStr[]
// ──────────────────────────────────────────────────────────────────────────────
void drawReadings(int16_t offsetX, int16_t offsetY) {
  oled.fillScreen(COLOR_BLACK);

  // Compute pixel-width of each line
  int widths[4];
  for (int i = 0; i < 4; i++) {
    widths[i] = strlen(lineStr[i]) * CHAR_WIDTH;
  }

  // Total height: 4 lines × 16px + 3 gaps × 8px = 88px
  const int totalBlockH = 4 * CHAR_HEIGHT + 3 * 8;
  const int yStart = (SCREEN_HEIGHT - totalBlockH) / 2; // = 20px

  oled.setTextSize(TEXT_SIZE);

  // Line 0: “TEMP: XXF” in RED
  oled.setTextColor(COLOR_RED);
  int16_t x0 = (SCREEN_WIDTH - widths[0]) / 2 + offsetX;
  int16_t y0 = yStart + 0 * (CHAR_HEIGHT + 8) + offsetY;
  oled.setCursor(x0, y0);
  oled.print(lineStr[0]);

  // Line 1: “L:XX H:YY” in RED
  int16_t x1 = (SCREEN_WIDTH - widths[1]) / 2 + offsetX;
  int16_t y1 = yStart + 1 * (CHAR_HEIGHT + 8) + offsetY;
  oled.setCursor(x1, y1);
  oled.print(lineStr[1]);

  // Line 2: “HUMID: ZZ%” in BLUE
  oled.setTextColor(COLOR_BLUE);
  int16_t x2 = (SCREEN_WIDTH - widths[2]) / 2 + offsetX;
  int16_t y2 = yStart + 2 * (CHAR_HEIGHT + 8) + offsetY;
  oled.setCursor(x2, y2);
  oled.print(lineStr[2]);

  // Line 3: “L:AA H:BB” in BLUE
  int16_t x3 = (SCREEN_WIDTH - widths[3]) / 2 + offsetX;
  int16_t y3 = yStart + 3 * (CHAR_HEIGHT + 8) + offsetY;
  oled.setCursor(x3, y3);
  oled.print(lineStr[3]);
}

// ──────────────────────────────────────────────────────────────────────────────
// 4) getHtmlPage() → return your new, mobile-friendly HTML/CSS/JS (with real fetch)
// ──────────────────────────────────────────────────────────────────────────────
String getHtmlPage() {
  // We embed your updated HTML exactly (with revised script to call fetch on /sensor-data).
  return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Potato Sensor</title>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap');
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Inter', sans-serif;
            background: linear-gradient(135deg, #ffd6e8 0%, #e8f4fd 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 15px;
        }
        
        .container {
            max-width: 700px;
            width: 100%;
        }
        
        .header {
            font-size: 72px;
            font-weight: 600;
            color: #C8860D;
            margin-bottom: 40px;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 20px;
            flex-wrap: wrap;
        }
        
        .potato-icon {
            width: 150px;
            height: 150px;
            display: inline-block;
            flex-shrink: 0;
        }
        
        .weather-card {
            background: rgba(255, 255, 255, 0.9);
            backdrop-filter: blur(10px);
            border-radius: 32px;
            padding: 70px;
            box-shadow: 0 20px 40px rgba(0, 0, 0, 0.1);
            border: 1px solid rgba(255, 255, 255, 0.2);
        }
        
        .metrics-container {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 80px;
            margin-bottom: 20px;
        }
        
        .metric-section h2 {
            font-size: 42px;
            font-weight: 600;
            color: #2c3e50;
            margin-bottom: 25px;
        }
        
        .metric-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 16px;
        }
        
        .metric-label {
            font-size: 24px;
            font-weight: 500;
            color: #8b9cb5;
        }
        
        .metric-value {
            font-size: 24px;
            font-weight: 600;
            color: #2c3e50;
        }
        
        .current-values {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 80px;
            align-items: center;
        }
        
        .current-humidity {
            text-align: center;
            margin-top: 30px;
        }
        
        .current-temperature {
            text-align: center;
            margin-top: 30px;
        }
        
        .current-value {
            font-size: 96px;
            font-weight: 700;
            line-height: 1;
        }
        
        .humidity-value {
            color: #4A90E2;
        }
        
        .temperature-value {
            color: #FF6B6B;
        }
        
        .last-updated {
            text-align: center;
            margin-top: 20px;
            font-size: 14px;
            color: #8b9cb5;
        }
        
        /* Smooth transitions for value changes */
        .current-value, .metric-value {
            transition: all 0.3s ease;
        }
        
        /* Enhanced responsive design */
        @media (max-width: 768px) {
            .weather-card {
                padding: 40px 25px;
            }
            
            .metrics-container {
                gap: 50px;
            }
            
            .current-values {
                gap: 50px;
            }
            
            .current-value {
                font-size: 72px;
            }
            
            .metric-section h2 {
                font-size: 32px;
            }
            
            .header {
                font-size: 48px;
            }
            
            .potato-icon {
                width: 120px;
                height: 120px;
            }
        }
        
        @media (max-width: 480px) {
            body {
                padding: 15px 10px 30px 10px;
                align-items: flex-start;
                min-height: 100vh;
                background-attachment: fixed;
            }
            
            .container {
                width: 100%;
                min-height: calc(100vh - 45px);
                display: flex;
                flex-direction: column;
                justify-content: flex-start;
            }
            
            .header {
                font-size: 36px;
                flex-direction: column;
                gap: 15px;
                margin-bottom: 25px;
                margin-top: 20px;
            }
            
            .potato-icon {
                width: 100px;
                height: 100px;
            }
            
            .weather-card {
                padding: 30px 20px 40px 20px;
                border-radius: 24px;
                margin-bottom: 20px;
                flex: 1;
                display: flex;
                flex-direction: column;
            }
            
            .metrics-container {
                grid-template-columns: 1fr;
                gap: 35px;
                margin-bottom: 20px;
                text-align: center;
            }
            
            .current-values {
                display: none;
            }
            
            .current-value {
                font-size: 64px;
            }
            
            .metric-section h2 {
                font-size: 28px;
                margin-bottom: 20px;
            }
            
            .metric-label, .metric-value {
                font-size: 20px;
            }
        }
        
        @media (max-width: 375px) {
            .current-value {
                font-size: 56px;
            }
            
            .header {
                font-size: 32px;
                margin-top: 15px;
            }
            
            .metric-section h2 {
                font-size: 24px;
            }
            
            .metric-label, .metric-value {
                font-size: 18px;
            }
            
            .weather-card {
                padding: 25px 15px 35px 15px;
            }
            
            body {
                padding: 10px 10px 25px 10px;
            }
        }
        
        /* Fix for very tall phones in portrait */
        @media (max-height: 700px) and (max-width: 480px) {
            body {
                align-items: flex-start;
                padding-top: 20px;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1 class="header">
            <svg class="potato-icon" viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">
                <!-- Potato body -->
                <ellipse cx="50" cy="55" rx="28" ry="35" fill="#D4A574" stroke="#B8956A" stroke-width="2"/>
                
                <!-- Potato eyes (little spots) -->
                <ellipse cx="40" cy="45" rx="3" ry="2" fill="#8B7355"/>
                <ellipse cx="60" cy="40" rx="2" ry="3" fill="#8B7355"/>
                <ellipse cx="45" cy="65" rx="2" ry="2" fill="#8B7355"/>
                <ellipse cx="58" cy="70" rx="3" ry="2" fill="#8B7355"/>
                
                <!-- Cute face -->
                <circle cx="42" cy="50" r="2" fill="#654321"/>
                <circle cx="58" cy="50" r="2" fill="#654321"/>
                <path d="M 46 60 Q 50 65 54 60" stroke="#654321" stroke-width="2" fill="none" stroke-linecap="round"/>
                
                <!-- Small highlight -->
                <ellipse cx="45" cy="42" rx="4" ry="6" fill="#E8C49A" opacity="0.7"/>
            </svg>
            Potato Sensor
        </h1>
        
        <div class="weather-card">
            <div class="metrics-container">
                <div class="metric-section">
                    <h2>Humidity</h2>
                    <div class="metric-row">
                        <span class="metric-label">High</span>
                        <span class="metric-value" id="humidity-high">--%</span>
                    </div>
                    <div class="metric-row">
                        <span class="metric-label">Low</span>
                        <span class="metric-value" id="humidity-low">--%</span>
                    </div>
                    <div class="current-humidity">
                        <div class="current-value humidity-value" id="current-humidity">--%</div>
                    </div>
                </div>
                
                <div class="metric-section">
                    <h2>Temperature</h2>
                    <div class="metric-row">
                        <span class="metric-label">High</span>
                        <span class="metric-value" id="temp-high">--°</span>
                    </div>
                    <div class="metric-row">
                        <span class="metric-label">Low</span>
                        <span class="metric-value" id="temp-low">--°</span>
                    </div>
                    <div class="current-temperature">
                        <div class="current-value temperature-value" id="current-temperature">--°</div>
                    </div>
                </div>
            </div>
            
            <div class="last-updated" id="last-updated">
                Last updated: Never
            </div>
        </div>
    </div>

    <script>
        // Called whenever we get new JSON from /sensor-data
        function updateSensorData(data) {
            // Current readings
            document.getElementById('current-temperature').textContent = Math.round(data.temperature) + '°';
            document.getElementById('current-humidity').textContent    = Math.round(data.humidity)    + '%';

            // Min/Max from JSON
            document.getElementById('temp-low').textContent      = Math.round(data.temp_low)    + '°';
            document.getElementById('temp-high').textContent     = Math.round(data.temp_high)   + '°';
            document.getElementById('humidity-low').textContent  = Math.round(data.hum_low)     + '%';
            document.getElementById('humidity-high').textContent = Math.round(data.hum_high)    + '%';

            // “Last updated”: convert UNIX timestamp (seconds) to JS Date
            const tsMs = data.last_updated * 1000; 
            const dt   = new Date(tsMs);
            document.getElementById('last-updated').textContent =
                'Last updated: ' + dt.toLocaleString();
        }

        // Fetch JSON from ESP32 every 3 seconds
        function fetchSensorData() {
            fetch('/sensor-data')
                .then(response => response.json())
                .then(json => updateSensorData(json))
                .catch(error => console.error('Error fetching sensor-data:', error));
        }

        setInterval(fetchSensorData, 3000);
        fetchSensorData(); // Initial call when page loads
    </script>
</body>
</html>
  )rawliteral";
}
