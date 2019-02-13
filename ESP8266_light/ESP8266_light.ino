/*
* https://github.com/esp8266/Arduino/blob/master/libraries/ESP8266WiFi/examples/WiFiWebServer/WiFiWebServer.ino
*
* Homebridge:
* http://server_ip/lamp/off
* http://server_ip/lamp/on
* http://server_ip/io_status/
* http://server_ip/lvl_status/
* http://server_ip/lvl/
* http://server_ip/hue/
* http://server_ip/hue_status/
* http://server_ip/sat/
* http://server_ip/sat_status/
* http://server_ip/dht/
* http://server_ip/rf3/off
* http://server_ip/rf3/on
* http://server_ip/rf3/io_status/
*
* OTA Update
* http://server_ip/ota/
*
* To upload a new version of the sketch, browse http://server_ip/ota/ first...
* this will set the ESP8266 in OTA Mode
*/


/*
* Libraries
* <> => in library folder
* "" => look in the sketch folder first
*/
#include <ESP8266WiFi.h>
/*#include <ESP8266WiFiMulti.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>*/
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <DHT.h>
#include <RCSwitch.h>
#include <EEPROM.h>


/*
* Global variables
*/
#define PWMRANGE    1023 // ESP8266 -> maximum pwm value -> 10bit resolution
#define RED_PIN     15   // D8 - Red channel
#define GREEN_PIN   12   // D6 - Green channel
#define BLUE_PIN    13   // D7 - Blue channel
#define OTA_BUTTON  4    // D2 - Push Button
#define D_INPUT     5    // D1 - Physical Switch
#define DHT_PIN     14   // D5 - DHT22 Humidity + Temperature Sensor
#define RF_TX       16   // D0 - RF 433MHz Transmitter
#define EEPROM_SIZE 512  // Size in byte you want to use from EEPROM
#define ADDR_STATE  0    // Address 0 in EEPROM
#define ADDR_HUE    1    // Address 1 in EEPROM
#define ADDR_SAT    2    // Address 2 in EEPROM
#define ADDR_LVL    3    // Address 3 in EEPROM

const char* ssid         = ""; // name of your wifi
const char* password     = ""; // password for wifi
const char* mdns_name    = ""; // mDNS name => <name>.local
const char* ota_name     = ""; // ota username
const char* ota_password = ""; // ota password

uint16_t server_port = 80;   // server port
uint16_t ota_port    = 8266; // ota port - 8266?

bool otaFlag = false; // ota enabled? - do not change this ever
bool output_state, rf3_state; // current state on/off

int _delay; // delay between brightness, hue & saturation steps
int phys_io_switch, _phys_io_switch; // physical - current, prev
int ota_io_button, _ota_io_button; // ota button - current, prev
int r, g, b; // output values
int hue, i_hue, hue_direction; // color
int sat, i_sat, sat_direction; // saturation
int lvl, i_lvl, lvl_direction; // brightness

String readRequest, response, jsonResponse;
String endOfHeader = "\r\n\r\n";
String newLine     = "\r\n";

DHT dht(DHT_PIN, DHT22); // pin, model
RCSwitch mySwitch = RCSwitch();

int rf3_code_on  = 4117; // RF3 on
int rf3_code_off = 4116; // RF3 off
float humidity, temp_c, temp_f; // DHT

uint32_t prev_ms_dht, interval_dht, prev_ms_wifi, interval_wifi; // time vars (unsigned long)

WiFiServer server(server_port); // server instance; listen port 80


void setup() {
  output_state = false;
  rf3_state    = false;
  _delay       = 4; // delay between a change

  // HSB
  hue = 0;
  sat = 100;
  lvl = 0;

  // physical
  _phys_io_switch = 0; // do not change!
  phys_io_switch  = 0; // do not change!
  _ota_io_button  = 1; // do not change!
  ota_io_button   = 0; // do not change!

  // time variables (DHT)
  prev_ms_dht   = 0;
  interval_dht  = 2000; // DHT22 - 2s
  prev_ms_wifi  = 0;
  interval_wifi = 10000; // WiFi - 10s

  Serial.begin(115200);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(D_INPUT, INPUT_PULLUP);
  pinMode(OTA_BUTTON, INPUT_PULLUP);

  mySwitch.enableTransmit(RF_TX);
  /*mySwitch.setPulseLength(320);
  mySwitch.setProtocol(2); // default = 1
  mySwitch.setRepeatTransmit(15); // transmission repetitions*/

  WiFiStart();
  all_off();

  dht.begin();
  server.begin();

  if (mdns_name != "") {
    MDNS.begin(mdns_name); // mDNS responder for <hostname>.local
  }

  EEPROM.begin(EEPROM_SIZE);

  // read EEPROM values - what if empty?
  output_state = EEPROM.read(ADDR_STATE);
  hue = EEPROM.read(ADDR_HUE);
  sat = EEPROM.read(ADDR_SAT);
  lvl = EEPROM.read(ADDR_LVL);

  /*Serial.println(hue, DEC);
  Serial-println(sat, DEC);
  Serial.println(lvl, DEC);
  Serial-println(output_state, bool);*/
}


void loop() {
  if (otaFlag) {
    ota_toggle();
    ArduinoOTA.handle();
  } else {
    ota_toggle();
    phys_switch();

    if (WiFi.status() != WL_CONNECTED) {WiFiStart();} //while (WiFi.waitForConnectResult() != WL_CONNECTED) // reconnect if lost
    WiFiClient client = server.available();

    if (!client) {return;} // no client; restart loop
    //while (!client.available()) {delay(1);} // wait until client is available

    // read request
    while (client.connected()) {
      if (client.available()) {
        char c = client.read(); // returns one character
        readRequest += c; // adds new character to string
        //readRequest += client.readStringUntil('\r'); // read from client until terminator + add to string
        if (c == '\n') {break;} // new line marks end of request
      }
    }

    // prepare response http/json
    response += "HTTP/1.1 200 OK" + newLine;
    response += "Content-Type: text/html" + newLine;
    response += "Connection: close" + endOfHeader;
    jsonResponse += "HTTP/1.1 200 OK" + newLine;
    jsonResponse += "Content-Type: application/json; charset=utf-8" + newLine;
    jsonResponse += "Connection: close" + endOfHeader;

    // request include "lamp"
    if (readRequest.indexOf("/lamp/") != -1) {
      // request includes "/lamp/off" - turn lamp off
      if (readRequest.indexOf("/lamp/off") != -1) {
        if (output_state == true) {
          smooth_hsv(output_state, hue, sat, 0);
          output_state = false;
        }
        response += output_state;
        client.print(response);
      }

      // request includes "/lamp/on" - turn lamp on
      if (readRequest.indexOf("/lamp/on") != -1) {
        if (lvl == 0) {lvl = PWMRANGE;}
        smooth_hsv(output_state, hue, sat, lvl);
        output_state = true;
        response += output_state;
        client.print(response);
      }
    }

    // request includes "/hue/" - hue value in degree
    else if (readRequest.indexOf("/hue/") != -1) {
      char charBuf_hue[50];
      readRequest.toCharArray(charBuf_hue, 50);
      hue = atoi(strtok(charBuf_hue, "GET /hue/"));
      smooth_hsv(output_state, hue, sat, lvl);
      response += hue;
      client.print(response);
    }

    // request includes "/sat/" - saturation value from 0 to 100
    else if (readRequest.indexOf("/sat/") != -1) {
      char charBuf_sat[50];
      readRequest.toCharArray(charBuf_sat, 50);
      sat = atoi(strtok(charBuf_sat, "GET /sat/"));
      smooth_hsv(output_state, hue, sat, lvl);
      response += sat;
      client.print(response);
    }

    // request includes "/lvl/" - brightness level
    else if (readRequest.indexOf("/lvl/") != -1) {
      char charBuf[50];
      readRequest.toCharArray(charBuf, 50);
      lvl = atoi(strtok(charBuf, "GET /lvl/"));
      smooth_hsv(output_state, hue, sat, lvl);

      if (lvl != 0) {output_state = true;}
      else {output_state = false;}

      response += lvl;
      client.print(response);
    }

    // status request
    else if (readRequest.indexOf("/io_status/") != -1) {response += output_state; client.print(response);}
    else if (readRequest.indexOf("/lvl_status/") != -1) {response += lvl; client.print(response);}
    else if (readRequest.indexOf("/hue_status/") != -1) {response += hue; client.print(response);}
    else if (readRequest.indexOf("/sat_status/") != -1) {response += sat; client.print(response);}
    else if (readRequest.indexOf("/rf3/io_status/") != -1) {response += rf3_state; client.print(response);}

    // request includes "rf3"
    else if (readRequest.indexOf("/rf3/") != -1) {
      // request includes "/rf3/on" - turn rf switch on
      if (readRequest.indexOf("/rf3/on") != -1) {
        mySwitch.send(rf3_code_on, 24);
        rf3_state = true;
        response += rf3_code_on;
        client.print(response);
      }

      // request includes "/rf3/off" - turn rf switch off
      if (readRequest.indexOf("/rf3/off") != -1) {
        mySwitch.send(rf3_code_off, 24);
        rf3_state = false;
        response += rf3_code_off;
        client.print(response);
      }
    }

    // request includes "/dht/" - reads temp & humidity + response in json
    else if (readRequest.indexOf("/dht/") != -1) {
      dht22();
      jsonResponse += "{\"temperature\": " + String(temp_c) + ", \"humidity\": " + String(humidity) + "}";
      client.print(jsonResponse);
    }

    // request includes "/ota/" - enables ota feature
    else if (readRequest.indexOf("/ota/") != -1) {
      OTA();
      otaFlag = true;

      response += "<!DOCTYPE HTML>" + newLine;
      response += "<html>" + newLine;
      response += "ESP8266 is now in OTA Mode. In this mode you can upload new firmware to the device." + newLine;
      response += "</html>\n";
      client.print(response);
    }

    // request includes "/restart/" - to restart esp8266
    else if (readRequest.indexOf("/restart/") != -1) {
      response += "<!DOCTYPE HTML>" + newLine;
      response += "<html>" + newLine;
      response += "ESP8266 is now restarting." + newLine;
      response += "</html>\n";
      client.print(response);
      ESP.restart();
    }

    // request does not match any of the locations
    else {
      // header
      response  = "HTTP/1.1 404 Not Found" + newLine;
      response += "Content-Type: text/html" + newLine;
      response += "Connection: close" + endOfHeader;
      // body
      response += "<!DOCTYPE HTML>" + newLine;
      response += "<html>" + newLine;
      response += "404 - Not found" + newLine;
      response += "</html>\n";
      client.print(response);
      /*client.stop(); // stops request and throws error in browser*/
    }

    client.flush();
    readRequest = response = jsonResponse = ""; // empty all strings
  }
}


/*
* start wifi & connect
*/
void WiFiStart() {
  /*IPAddress ip(192,168,1,10); // fixed IP
  IPAddress gateway(192,168,1,1); // gateway (router) IP
  IPAddress subnet(255,255,255,0); // subnet mask
  WiFi.config(ip, gateway, subnet);
  WiFi.mode(WIFI_STA); // configure as wifi station*/
  uint32_t current_ms = millis(); //unsigned long

  if (current_ms - prev_ms_wifi >= interval_wifi) {
    prev_ms_wifi = current_ms;
    WiFi.begin(ssid, password); // connect to network

  // When several wifi option are avaiable... take the strongest
  /*wifiMulti.addAP("ssid_from_AP_1", "your_password_for_AP_1");
  wifiMulti.addAP("ssid_from_AP_2", "your_password_for_AP_2");
  wifiMulti.addAP("ssid_from_AP_3", "your_password_for_AP_3");*/
  }
  //Serial.println(WiFi.localIP()); // print IP address
}


/*
* immediately switch off all lights
*/
void all_off() {
  output_state = false;
  set_value(0,0,0);
}


/*
* output value to pin
*/
void set_value(int set_r, int set_g, int set_b) {
  analogWrite(RED_PIN, set_r);
  analogWrite(GREEN_PIN, set_g);
  analogWrite(BLUE_PIN, set_b);
}


/*
* write values to eeprom
*/
void write_to_eeprom() {
  EEPROM.write(ADDR_STATE, output_state);
  EEPROM.write(ADDR_HUE, hue);
  EEPROM.write(ADDR_SAT, sat);
  EEPROM.write(ADDR_LVL, lvl);
  //EEPROM.commit(); // only commit
  EEPROM.end(); // commit + release RAM of content
}


/*
* Smooth transition to new color, saturation or brightness
* direction:
* 0 - down; counter-clockwise
* 1 - up; clockwise
* 2 - not changed
*/
void smooth_hsv(bool _state, int _hue, int _sat, int _lvl) {
  int temp_hue, temp_sat, temp_lvl;
  int i_max; // largest difference

  if (_state == false) {i_lvl = 0;} // currently off -> switch on

  // check directions
  if (i_hue > _hue) {temp_hue = i_hue - _hue;}
  else if (i_hue < _hue) {temp_hue = _hue - i_hue;}

  if (i_hue == _hue) {
    hue_direction = 2;
    temp_hue = 0;
  }
  else if (temp_hue > 180) {hue_direction = 0;} // counter-clockwise
  else if (temp_hue < 180) {hue_direction = 1;} // clockwise

  if (i_sat > _sat) {
    sat_direction = 0;
    temp_sat = i_sat - _sat;
  }
  else if (i_sat < _sat) {
    sat_direction = 1;
    temp_sat = _sat - i_sat;
  }
  else {
    sat_direction = 2;
    temp_sat = 0;
  }

  if (i_lvl > _lvl) {
    lvl_direction = 0;
    temp_lvl = i_lvl - _lvl;
  }
  else if (i_lvl < _lvl) {
    lvl_direction = 1;
    temp_lvl = _lvl - i_lvl;
  }
  else {
    lvl_direction = 2;
    temp_lvl = 0;
  }

  // set maximum
  if (temp_hue == 0 && temp_sat == 0 && temp_lvl == 0) {i_max = -1;}
  else {
    if (temp_hue >= temp_sat) {i_max = temp_hue;}
    else {i_max = temp_sat;}

    if (i_max >= temp_lvl) {i_max = i_max;}
    else {i_max = temp_lvl;}
  }

  // smooth brightness change loop
  for (int i_end = 0; i_end < i_max; i_end++) {

    // decrease or increase
    if (hue_direction != 2) {
      if (hue_direction == 1) {
        if (i_hue == 360) {i_hue = 0;}
        i_hue++;
      }
      else if (hue_direction == 0) {
        if (i_hue == 0) {i_hue = 360;}
        i_hue--;
      }
    }

    if (sat_direction != 2) {
      if (sat_direction == 1) {i_sat++;}
      else if (sat_direction == 0) {i_sat--;}
    }

    if (lvl_direction != 2) {
      if (lvl_direction == 1) {i_lvl++;}
      else if (lvl_direction == 0) {i_lvl--;}
    }

    // reached maximum? - set direction to "not changed"
    if (i_lvl == _lvl) {lvl_direction = 2;}
    if (i_hue == _hue) {hue_direction = 2;}
    if (i_sat == _sat) {sat_direction = 2;}

    // convert to rgb & output with a delay between changes
    hsv2rgb(i_hue, i_sat, i_lvl);
    set_value(r, g, b);
    delay(_delay);
  }

  // save new before state
  _lvl = i_lvl;
  _hue = i_hue;
  _sat = i_sat;

  write_to_eeprom(); // write after smooth transition
}


/*
* check whether physical switch is toggled or not
* does not indicate the same status twice in a row
*/
void phys_switch() {
  phys_io_switch = digitalRead(D_INPUT);

  if (phys_io_switch != _phys_io_switch) {
    if (phys_io_switch == HIGH) {
      smooth_hsv(output_state, hue, sat, 0);
      output_state = false;
    } else {
      smooth_hsv(output_state, hue, sat, 100);
      output_state = true;
    }
  }

  _phys_io_switch = phys_io_switch; // new before state
  //delay(_delay);
}


/*
* toggles OTA mode - via button on board
*/
void ota_toggle() {
  ota_io_button = digitalRead(OTA_BUTTON);

  if (ota_io_button != _ota_io_button && ota_io_button == 1) { // toggle when input changed
    otaFlag = !otaFlag;
  }

  _ota_io_button = ota_io_button;
}


/*
* DHT22 read
* prevent reading within a certain amount of time
*/
void dht22() {
  uint32_t current_ms = millis(); //unsigned long

  if (current_ms - prev_ms_dht >= interval_dht) {
    prev_ms_dht = current_ms;

    humidity = dht.readHumidity();
    temp_c = dht.readTemperature(false); // celsius
    temp_f = dht.readTemperature(true); // fahrenheit

    if (isnan(humidity) || isnan(temp_c) || isnan(temp_f)) {
      humidity = 0;
      temp_c = 0;
      temp_f = 0;
      return;
    }

    float hic = dht.computeHeatIndex(temp_c, humidity, false); // celsius
    float hif = dht.computeHeatIndex(temp_f, humidity, true); // fahrenheit
  }
}


/*
* OTA - Over the air update
* handles ota call
* restarts ESP after finishing
*/
void OTA() {
  Serial.println("OTA");

  /*ArduinoOTA.setPort(ota_port);
  ArduinoOTA.setHostname(ota_name);
  ArduinoOTA.setPassword(ota_password); // (const char *)"password123"*/

  ArduinoOTA.onStart([]() {
    all_off();
  });

  ArduinoOTA.onEnd([]() {
    all_off();
    otaFlag = false;
    Serial.flush();
    ESP.restart();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    all_off();
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    all_off();
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
}


/*
* convert hsv values to rgb
* output values multiplied by PWMRANGE
*/
void hsv2rgb(float h, float s, float v) {
  int i;
  float f, p, q, t, _r, _g, _b;

  h /= 360; // hue
  s /= 100; // saturation
  v /= 100; // value or brightness

  if (s == 0) { // achromatic (grey)
    r = g = b = v;
    return;
  }

  i = floor(h * 6);
  f = h * 6 - i;
  p = v * (1 - s);
  q = v * (1 - f * s);
  t = v * (1 - (1 - f) * s);

  switch(i) {
    case 0:
      _r = v;
      _g = t;
      _b = p;
      break;
    case 1:
      _r = q;
      _g = v;
      _b = p;
      break;
    case 2:
      _r = p;
      _g = v;
      _b = t;
      break;
    case 3:
      _r = p;
      _g = q;
      _b = v;
      break;
    case 4:
      _r = t;
      _g = p;
      _b = v;
      break;
    case 5:
      _r = v;
      _g = p;
      _b = q;
      break;
    }

    r = round(_r * PWMRANGE);
    g = round(_g * PWMRANGE);
    b = round(_b * PWMRANGE);
}
