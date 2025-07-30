# 🥔 Potato Storage Monitor

A comprehensive ESP32-based environmental monitoring system designed specifically for potato storage rooms. This project combines real-time temperature and humidity monitoring with both a local OLED display and a mobile-friendly web interface.

## Features

- **Real-time Monitoring**: Continuous temperature (°F) and humidity (%) tracking
- **Min/Max Tracking**: Automatic recording of daily temperature and humidity extremes
- **Dual Display**: 
  - Local 1.5" color OLED display with burn-in prevention
  - Mobile-responsive web interface with potato-themed design
- **WiFi Connectivity**: Remote monitoring via any web browser
- **REST API**: JSON endpoint for integration with other systems
- **NTP Time Sync**: Accurate timestamps for all readings
- **Auto-refresh**: Web interface updates every 3 seconds

## Hardware Requirements

### Main Components
- **ESP32 DevKit** (or compatible board)
- **DHT22** temperature/humidity sensor
- **Waveshare 1.5" SSD1351 OLED Display** (128×128, SPI)

### Pin Connections

| Component | ESP32 Pin | Function |
|-----------|-----------|----------|
| DHT22 | GPIO 22 | Data |
| OLED CS | GPIO 5 | Chip Select |
| OLED DC | GPIO 16 | Data/Command |
| OLED RST | GPIO 17 | Reset |
| OLED MOSI | GPIO 23 | SPI Data |
| OLED SCLK | GPIO 18 | SPI Clock |

**Note**: OLED uses VSPI interface, MISO not required.

## Required Libraries

Install these libraries via Arduino IDE Library Manager:

```
- Adafruit GFX Library
- Adafruit SSD1351 Library
- DHT sensor library (Adafruit Unified)
- ArduinoJson
- ESP32 WiFi (built-in)
- ESP32 WebServer (built-in)
```

## Installation & Setup

### 1. Hardware Assembly
1. Connect the DHT22 sensor to GPIO 22
2. Wire the SSD1351 OLED according to the pin table above
3. Ensure proper 3.3V/5V power connections

### 2. Software Configuration
1. Clone or download this project
2. Open `potato_sensor.ino` in Arduino IDE
3. Update WiFi credentials in the configuration section:
   ```cpp
   const char* ssid     = "YourWiFiSSID";
   const char* password = "YourWiFiPassword";
   ```
4. Select **ESP32 Dev Module** as your board
5. Upload the code to your ESP32

### 3. First Run
1. Open Serial Monitor (115200 baud) to view startup process
2. Note the IP address displayed after WiFi connection
3. Navigate to `http://[ESP32-IP-ADDRESS]` in your browser
4. The OLED will show real-time readings with color coding:
   - **Red text**: Temperature data
   - **Blue text**: Humidity data

## Web Interface

### Main Dashboard
- **Temperature**: Current reading with daily high/low
- **Humidity**: Current reading with daily high/low  
- **Responsive Design**: Optimized for mobile devices
- **Auto-refresh**: Updates every 3 seconds
- **Cute Design**: Potato-themed interface perfect for storage monitoring

### API Endpoints

#### GET `/`
Returns the full HTML dashboard

#### GET `/sensor-data`
Returns JSON with current sensor readings:
```json
{
  "temperature": 68.5,
  "humidity": 45.2,
  "temp_low": 65.1,
  "temp_high": 72.3,
  "hum_low": 42.8,
  "hum_high": 48.6,
  "last_updated": 1643723400
}
```

## Advanced Features

### OLED Burn-in Prevention
The display automatically shifts content by 1 pixel every minute in a 4-phase cycle to prevent screen burn-in.

### Optimal Potato Storage Conditions
- **Temperature**: 45-50°F (7-10°C)
- **Humidity**: 80-90% RH
- **Ventilation**: Required to prevent sprouting

Monitor these ranges to ensure optimal potato storage conditions!

## Troubleshooting

### Common Issues

**WiFi Connection Failed**
- Verify SSID and password in code
- Check WiFi signal strength
- Ensure 2.4GHz network (ESP32 doesn't support 5GHz)

**OLED Display Not Working**
- Verify SPI pin connections
- Check power supply (3.3V for OLED)
- Ensure proper ground connections

**DHT22 Reading Errors**
- Check data pin connection (GPIO 22)
- Verify sensor power (3.3V or 5V depending on module)
- Allow 2+ seconds between readings

**Web Interface Not Loading**
- Check Serial Monitor for IP address
- Verify ESP32 and device are on same network
- Try accessing via IP address instead of hostname

### Serial Monitor Output
Monitor debug information:
```
Connecting to Wi-Fi SSID "YourNetwork" …
Wi-Fi connected. IP = 192.168.1.100
NTP synced, current UNIX time = 1643723400
HTTP server started
```

## Customization

### Changing Update Intervals
- **Sensor readings**: Modify `2000UL` in the DHT read condition
- **Web refresh**: Change `3000` in the JavaScript setInterval
- **OLED shift**: Adjust `60000UL` for burn-in prevention timing

### Display Rotation
The OLED is rotated 90° clockwise by default. Modify this line to change orientation:
```cpp
oled.setRotation(3); // 0=0°, 1=90°, 2=180°, 3=270°
```

## Contributing

Feel free to submit issues, feature requests, or pull requests to improve this potato storage monitoring system!

## License

This project is open source. Feel free to modify and distribute as needed for your potato storage needs.

---

**Perfect for**: Home root cellars, commercial potato storage, agriculture monitoring, or any environment where precise temperature and humidity control is critical. 🥔
