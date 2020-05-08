#include <OneWire.h>

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>

// OneWire DS18S20, DS18B20, DS1822 Temperature standard example, combined with some code to start a WebServer, and allow for OTA updates.
// The web server will respond to a query to the root (/) with the temperature value (celsius) using two decimals behind the comma (dot).
// If the sensor does not respond, the temperature returned by the web server will get set to 0.0.
//
// The middle pin of the DS18B20 should be connected to pin D4. The left pin (look at flat side, legs down) must be connected to GND,
// and the right pin must be connected to 5V. Normally everyone tells you to put a pull-up resistor of 4K7 between D4 and 5V, but I had to
// remove it to get the sensor to work. Strange... Maybe my ESP8266 (D1 mini clone) does not like the 5V to be fed back into the input pin directly?
// Or my DS18B20 clone component does not need it. Just try with and without pull-up resistor (try 4K7, without, and 1K) to see what works best.
// Initially I soldered my DS18B20 directly to the ESP board, but that immediately increased the temperature to somewhere in the 30 degrees celsius,
// while it was just 21 degrees. The ESP board is a bit warmer than the environment sometimes. So I used 20 cm wires in between to fix that issue.
//
// 5/5/2020, Thijs Kaper.
//
// http://www.pjrc.com/teensy/td_libs_OneWire.html
//
// The DallasTemperature library can do all this work for you!
// https://github.com/milesburton/Arduino-Temperature-Control-Library

OneWire  ds(D4);  // on pin D4 (a 4.7K resistor is necessary -> nope, I needed to remove the one on my board to get it to work...)

// Define a web server on port 80
ESP8266WebServer server ( 80 );

// Hardcoded network
const char* ssid = "YOUR-SSID";
const char* password = "YOUR-PASSWORD";

// Global variable, returned by webserver "GET /" read call, updated every second by DS18B20 measurement.
float temperature = 0;

// Flag indicating we did find a DS18B20.
boolean foundOne = false;

// Debug flag, can be used to stop measuring (also set during OTA udpate).
boolean runMeasurements = true;

// Initialize eveything
void setup(void) {
  
  String bootReason = "\n\n\nBootreason: " + ESP.getResetInfo();
  Serial.begin(9600);
  Serial.println(bootReason);

  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  Serial.println("Connected");

  // Set OTA device name, and OTA password (change the "xx" into a suitable password)
  ArduinoOTA.setHostname("tempsense");
  ArduinoOTA.setPassword((const char *)"xx");

  ArduinoOTA.onStart([]() {
    Serial.println("Firmware update start");
    runMeasurements = false;
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  
  Serial.println("\nReady");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // init server
  server.on ( "/", []() {
    char buf[255];
    snprintf (buf, 255, "%1.2f\n", temperature);
    server.send (200, "text/plain", buf);  
  } );
  server.on ( "/stop", []() {
    runMeasurements = false;
    server.send ( 200, "text/plain", "stop measuring" );
  } );
  server.on ( "/start", []() {
    runMeasurements = true;
    server.send ( 200, "text/plain", "start measuring" );
  } );
  server.onNotFound ( handleNotFound );
  server.begin();

}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }

  server.send ( 404, "text/plain", message );
}

void resetTemperatureValue() {
  temperature = 0.0;
  Serial.println("---- indicate missing temperature measurement by setting to 0.0 ----");
}

void loop(void) {
  ArduinoOTA.handle();
  server.handleClient();

  if (!runMeasurements) {
    temperature = 0.0;
    return;
  }
  
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];
  float celsius, fahrenheit;

  if ( !ds.search(addr)) {
    // Two times "no more addresses" in a row will reset the temperature to report 0.0.
    if (!foundOne) {
      resetTemperatureValue();
    }
    foundOne = false;
    
    Serial.println("No more addresses.");
    Serial.println();
    ds.reset_search();
    delay(250);
    return;
  }
  // We found a one-wire thing
  foundOne = true;
  
  Serial.print("ROM =");
  for( i = 0; i < 8; i++) {
    Serial.write(' ');
    Serial.print(addr[i], HEX);
  }

  if (OneWire::crc8(addr, 7) != addr[7]) {
    resetTemperatureValue();
    Serial.println("CRC is not valid!");
    return;
  }
  Serial.println();
 
  // the first ROM byte indicates which chip
  switch (addr[0]) {
    case 0x10:
      Serial.println("  Chip = DS18S20");  // or old DS1820
      type_s = 1;
      break;
    case 0x28:
      Serial.println("  Chip = DS18B20");
      type_s = 0;
      break;
    case 0x22:
      Serial.println("  Chip = DS1822");
      type_s = 0;
      break;
    default:
      resetTemperatureValue();
      Serial.println("Device is not a DS18x20 family device.");
      return;
  } 

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  
  delay(1000);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.
  
  present = ds.reset();
  ds.select(addr);    
  ds.write(0xBE);         // Read Scratchpad

  Serial.print("  Data = ");
  Serial.print(present, HEX);
  Serial.print(" ");
  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.print(" CRC=");
  Serial.print(OneWire::crc8(data, 8), HEX);
  Serial.println();

  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }

  celsius = (float)raw / 16.0;
  fahrenheit = celsius * 1.8 + 32.0;
  Serial.print("  Temperature = ");
  Serial.print(celsius);
  Serial.print(" Celsius, ");
  Serial.print(fahrenheit);
  Serial.println(" Fahrenheit");

  // Update the global variable which is read by the webserver root (/) call.
  temperature = celsius;
}
