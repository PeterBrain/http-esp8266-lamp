/*
* HomeBridge HTTP esp8266 Light
*/

/*
* Libraries
* <> => in library folder
* "" => look in the sketch folder first
*/
#include <ESP8266WiFi.h>
/*#include <ESP8266WiFiMulti.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>*/
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <DHT.h>
#include <RCSwitch.h>


/*
* Global variables
*/
#define PWMRANGE     1023 // ESP8266 -> maximum pwm value -> 10bit resolution
#define RED_PIN      15   // D8 - Red channel
#define GREEN_PIN    12   // D6 - Green channel
#define BLUE_PIN     13   // D7 - Blue channel
#define DHT_PIN      14   // D5 - DHT22 Humidity + Temperature Sensor
#define BUILTIN_LED  2    // D4 - bulitin LED
#define FLASH_BUTTON 0    // D3 - Flash button
#define OTA_BUTTON   4    // D2 - Push Button
#define D_INPUT      5    // D1 - Physical Switch
#define RF_TX        16   // D0 - RF 433MHz Transmitter
#define LDR          A0   // ADC - Light dependent resistor (LDR), Photodiode
#define EEPROM_SIZE  512  // Size in byte you want to use from EEPROM

#define ADDR_STATE   0    // Address 0 in EEPROM
#define ADDR_HUE     1    // Address 1 in EEPROM
#define ADDR_SAT     2    // Address 2 in EEPROM
#define ADDR_LVL     3    // Address 3 in EEPROM

#define MQTT_WILL_TOPIC   ""   // mqtt will topic
#define MQTT_WILL_PAYLOAD "offline"   // mqtt will payload
#define MQTT_WILL_QOS     1    // mqtt will QoS
#define MQTT_WILL_RETAIN  true // mqtt will retain

const char* wifi_ssid    = ""; // wifi name
const char* wifi_passwd  = ""; // wifi password
const char* mdns_name    = ""; // mDNS name => <name>.local
const char* ota_name     = ""; // ota username
const char* ota_passwd   = ""; // ota password
const char* mqtt_server  = ""; // mqtt server ip or hostname
const char* mqtt_id      = ""; // mqtt id for the client
const char* mqtt_user    = ""; // mqtt username
const char* mqtt_passwd  = ""; // mqtt password
const char* mqtt_topic_0 = ""; // mqtt topic (device status)
const char* mqtt_topic_1 = ""; // mqtt topic to subscribe to

uint16_t server_port = 80;   // server port
uint16_t ota_port    = 8266; // ota port
uint16_t mqtt_port   = 1883; // mqtt port

uint16_t rf1_code_on  = 21;    // RF1 on
uint16_t rf1_code_off = 20;    // RF1 off
uint16_t rf2_code_on  = 16405; // RF2 on
uint16_t rf2_code_off = 16404; // RF2 off
uint16_t rf3_code_on  = 4117;  // RF3 on
uint16_t rf3_code_off = 4116;  // RF3 off

uint8_t fade_delay       = 6;    // delay between a change - 6ms - delay between brightness, hue & saturation steps
uint8_t ota_led_interval = 100;  // delay between status led off and on (ota blinking)
uint32_t interval_dht    = 2000; // DHT22 - 2s
uint32_t prev_ms_dht; // time vars (unsigned long)

bool otaFlag, output_state, rf1_state, rf2_state, rf3_state;

uint8_t phys_io_switch, _phys_io_switch; // physical - current, prev
uint8_t ota_io_button, _ota_io_button; // ota button - current, prev
uint16_t r_value, g_value, b_value; // output values
uint16_t hue, i_hue, sat, i_sat, lvl, i_lvl; // hue, saturation, brightness

float r; // dimming curve factor
float humidity, temp_c, temp_f; // DHT

String http_header_content_html = "Content-Type: text/html"; // content = html
String http_header_content_json = "Content-Type: application/json; charset=utf-8"; // content = json
String page_not_found           = "404 - Not Found"; // text for error 404
String newLine                  = "\r\n"; // carriage return & new line

IPAddress ip      (192,168,1,10);  // fixed IP (0,0,0,0)
IPAddress subnet  (255,255,255,0); // subnet mask
IPAddress gateway (192,168,1,1);   // gateway (router) IP
IPAddress dns1    (1,1,1,1);       // DNS server (Cloudflare)
IPAddress dns2    (1,0,0,1);       // DNS server (Cloudflare)

DHT dht(DHT_PIN, DHT22); // pin, model
RCSwitch RF_Switch = RCSwitch(); // RF switch
WiFiServer server(server_port); // server instance; listen port 80
WiFiClient client; // instantiate wifi client object
PubSubClient MQTTclient(client); // instantiate mqtt client object


/*
* init on startup
*/
void setup() {
  otaFlag = output_state = rf1_state = rf2_state = rf3_state = false;

  // init HSB values (if EEPROM empty)
  hue = 330;
  sat = 75;
  lvl = 0; // light is always off after a restart (except eeprom values exist)

  // init button values
  _phys_io_switch = 0; // do not change!
  phys_io_switch  = 0; // do not change!
  _ota_io_button  = 1; // do not change!
  ota_io_button   = 0; // do not change!

  // time variables
  prev_ms_dht = 0;

  Serial.begin(115200);

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(BUILTIN_LED, OUTPUT); // inverted; HIGH - off; LOW - on
  pinMode(D_INPUT, INPUT_PULLUP); // 1 - not pressed; 0 - pressed
  pinMode(OTA_BUTTON, INPUT_PULLUP); // 1 - not pressed; 0 - pressed

  digitalWrite(BUILTIN_LED, HIGH); // inverted - led off

  RF_Switch.enableTransmit(RF_TX); // set RF pin
  /*RF_Switch.setPulseLength(320);
  RF_Switch.setProtocol(2); // default = 1
  RF_Switch.setRepeatTransmit(15); // transmission repetitions*/

  //WiFi.config(ip, gateway, subnet, dns1, dns2); // fixed ip and so on
  WiFi.mode(WIFI_STA); // configure as wifi station - auto reconnect if lost
  WiFi.begin(wifi_ssid, wifi_passwd); // connect to network - (ssid, password, channel, bssid, connect)

  // When several wifi option are avaiable... take the strongest
  /*wifiMulti.addAP("ssid_from_AP_1", "your_password_for_AP_1");
  wifiMulti.addAP("ssid_from_AP_2", "your_password_for_AP_2");
  wifiMulti.addAP("ssid_from_AP_3", "your_password_for_AP_3");*/

  /*Serial.println(WiFi.localIP()); // print IP address
  Serial.println(WiFi.macAddress()); // print MAC address*/

  all_off();

  dht.begin();
  server.begin();

  MQTTclient.setServer(mqtt_server, mqtt_port);
  MQTTclient.setCallback(mqtt_callback);

  if (mdns_name != "") {MDNS.begin(mdns_name);} // mDNS responder for <hostname>.local

  EEPROM.begin(EEPROM_SIZE); // define EEPROM size

  // read EEPROM values - what if empty?
  output_state = EEPROM.read(ADDR_STATE);
  hue = EEPROM.read(ADDR_HUE);
  sat = EEPROM.read(ADDR_SAT);
  lvl = EEPROM.read(ADDR_LVL);

  /*Serial-println(output_state, bool);
  Serial.println(hue, DEC);
  Serial-println(sat, DEC);
  Serial.println(lvl, DEC);*/

  if (output_state) {smooth_hsv(hue, sat, lvl);} // restore last state

  r = (100 * log10(2)) / (log10(100)); // pwmSteps * log10(2) / log10(maxPWMrange)
}


void loop() {
  ota_toggle();
  phys_switch();

  if (!wifi_status()) {return;} // not connected to network; restart loop

  if (otaFlag) {
    MDNS.update();
    ArduinoOTA.handle();
  } // handle ota

  if (!MQTTclient.connected()) { // not connected to mqtt server
    //String clientId = "ESP8266Client-" + String(random(0xffff), HEX);
    //if (MQTTclient.connect(clientId.c_str())) {

    if (MQTTclient.connect(
      mqtt_id,
      mqtt_user,
      mqtt_passwd,
      MQTT_WILL_TOPIC,
      MQTT_WILL_QOS,
      MQTT_WILL_RETAIN,
      MQTT_WILL_PAYLOAD
    )) {
      MQTTclient.publish(mqtt_topic_0, "online");
      MQTTclient.subscribe(mqtt_topic_1);
    } else {
      Serial.print("failed, rc=");
      Serial.print(MQTTclient.state());
    }
  }

  client = server.available();

  if (!client) {return;} // no client; restart loop
  //while (!client.available()) {delay(1);} // wait until client is available

  // read request
  String readRequest;
  while (client.connected()) {
    if (client.available()) {
      char c = client.read(); // returns one character
      readRequest += c; // adds new character to string
      //readRequest += client.readStringUntil('\r'); // read from client until terminator + add to string
      if (c == '\n') {break;} // new line marks end of request
    }
  }

  // lamp
  if (readRequest.indexOf("/lamp/") != -1) {
    // turn lamp off
    if (readRequest.indexOf("/lamp/off") != -1) {
      if (output_state == true) {
        lvl = 0;
        smooth_hsv(hue, sat, lvl);
      }
      client.print(buildHeader(200, http_header_content_html, String(output_state)));
    }

    // turn lamp on
    else if (readRequest.indexOf("/lamp/on") != -1) {
      if (lvl == 0) {lvl = 100;}
      smooth_hsv(hue, sat, lvl);
      client.print(buildHeader(200, http_header_content_html, String(output_state)));
    }

    // hue value in degree (0-359)
    else if (readRequest.indexOf("/lamp/hue/") != -1) {
      char charBuf_hue[50];
      readRequest.toCharArray(charBuf_hue, 50);
      hue = atoi(strtok(charBuf_hue, "GET /lamp/hue/"));
      smooth_hsv(hue, sat, lvl);
      client.print(buildHeader(200, http_header_content_html, String(hue)));
    }

    // saturation value from 0 to 100
    else if (readRequest.indexOf("/lamp/sat/") != -1) {
      char charBuf_sat[50];
      readRequest.toCharArray(charBuf_sat, 50);
      sat = atoi(strtok(charBuf_sat, "GET /lamp/sat/"));
      smooth_hsv(hue, sat, lvl);
      client.print(buildHeader(200, http_header_content_html, String(sat)));
    }

    // brightness level
    else if (readRequest.indexOf("/lamp/lvl/") != -1) {
      char charBuf[50];
      readRequest.toCharArray(charBuf, 50);
      lvl = atoi(strtok(charBuf, "GET /lamp/lvl/")); // strip down request
      smooth_hsv(hue, sat, lvl);
      client.print(buildHeader(200, http_header_content_html, String(lvl)));
    }

    // status
    else if (readRequest.indexOf("/status/") != -1) {
      if (readRequest.indexOf("/lamp/status/io") != -1) {client.print(buildHeader(200, http_header_content_html, String(output_state)));}
      else if (readRequest.indexOf("/lamp/status/hue") != -1) {client.print(buildHeader(200, http_header_content_html, String(hue)));}
      else if (readRequest.indexOf("/lamp/status/sat") != -1) {client.print(buildHeader(200, http_header_content_html, String(sat)));}
      else if (readRequest.indexOf("/lamp/status/lvl") != -1) {client.print(buildHeader(200, http_header_content_html, String(lvl)));}
      else {client.print(buildHeader(404, http_header_content_html, page_not_found));}
    }

    // resolution testing 8-bit 0-255
    else if (readRequest.indexOf("/lamp/test/255") != -1) {
      lvl = 100;
      set_value(255,255,255);
      client.print(buildHeader(200, http_header_content_html, "Lamp was set to 100% with 8-bit resolution"));
    }

    // resolution testing 10-bit 0-1023
    else if (readRequest.indexOf("/lamp/test/1023") != -1) {
      lvl = 100;
      set_value(PWMRANGE,PWMRANGE,PWMRANGE);
      client.print(buildHeader(200, http_header_content_html, "Lamp was set to 100% with 10-bit resolution"));
    }

    else {client.print(buildHeader(404, http_header_content_html, page_not_found));}
  }

  // rf1
  else if (readRequest.indexOf("/rf1/") != -1) {
    // turn on rf1
    if (readRequest.indexOf("/rf1/on") != -1) {
      RF_Switch.send(rf1_code_on, 24);
      rf1_state = true;
      client.print(buildHeader(200, http_header_content_html, String(rf1_code_on)));
    }

    // switch off rf1
    else if (readRequest.indexOf("/rf1/off") != -1) {
      RF_Switch.send(rf1_code_off, 24);
      rf1_state = false;
      client.print(buildHeader(200, http_header_content_html, String(rf1_code_off)));
    }

    // rf1 status
    else if (readRequest.indexOf("/rf1/status/io") != -1) {client.print(buildHeader(200, http_header_content_html, String(rf1_state)));}

    else {client.print(buildHeader(404, http_header_content_html, page_not_found));}
  }

  // rf2
  else if (readRequest.indexOf("/rf2/") != -1) {
    // turn on rf2
    if (readRequest.indexOf("/rf2/on") != -1) {
      RF_Switch.send(rf2_code_on, 24);
      rf2_state = true;
      client.print(buildHeader(200, http_header_content_html, String(rf2_code_on)));
    }

    // turn off rf2
    else if (readRequest.indexOf("/rf2/off") != -1) {
      RF_Switch.send(rf2_code_off, 24);
      rf2_state = false;
      client.print(buildHeader(200, http_header_content_html, String(rf2_code_off)));
    }

    // rf2 status
    else if (readRequest.indexOf("/rf2/status/io") != -1) {client.print(buildHeader(200, http_header_content_html, String(rf2_state)));}

    else {client.print(buildHeader(404, http_header_content_html, page_not_found));}
  }

  // rf3
  else if (readRequest.indexOf("/rf3/") != -1) {
    // turn on rf3
    if (readRequest.indexOf("/rf3/on") != -1) {
      RF_Switch.send(rf3_code_on, 24);
      rf3_state = true;
      client.print(buildHeader(200, http_header_content_html, String(rf3_code_on)));
    }

    // turn off rf3
    else if (readRequest.indexOf("/rf3/off") != -1) {
      RF_Switch.send(rf3_code_off, 24);
      rf3_state = false;
      client.print(buildHeader(200, http_header_content_html, String(rf3_code_off)));
    }

    // rf3 status
    else if (readRequest.indexOf("/rf3/status/io") != -1) {client.print(buildHeader(200, http_header_content_html, String(rf3_state)));}

    else {client.print(buildHeader(404, http_header_content_html, page_not_found));}
  }

  // read temp & humidity + response in json
  else if (readRequest.indexOf("/dht") != -1) {
    dht22();
    String json_content = "{\"temperature\":" + String(temp_c) + ",\"humidity\":" + String(humidity) + "}";
    client.print(buildHeader(200, http_header_content_json, json_content));
  }

  else if (readRequest.indexOf("/eeprom") != -1) {
    bool output_state_temp = EEPROM.read(ADDR_STATE);
    uint16_t hue_temp = EEPROM.read(ADDR_HUE);
    uint16_t sat_temp = EEPROM.read(ADDR_SAT);
    uint16_t lvl_temp = EEPROM.read(ADDR_LVL);

    client.print(buildHeader(200, http_header_content_html, "State: " + String(output_state_temp) + "<br>HUE: " + String(hue_temp) + "<br>SAT: " + String(sat_temp) + "<br>LVL: " + String(lvl_temp) + "<br><br>Should be:<br>State: " + String(output_state) + "<br>HUE: " + String(hue) + "<br>SAT: " + String(sat) + "<br>LVL: " + String(lvl)));
  }

  // enable ota service
  else if (readRequest.indexOf("/ota") != -1) {
    OTA();
    otaFlag = true;
    client.print(buildHeader(200, http_header_content_html, "Device is now in OTA Mode. In this mode you can upload new firmware to the device."));
  }

  // disable ota service
  else if (readRequest.indexOf("/ota/false") != -1) {
    otaFlag = false;
    client.print(buildHeader(200, http_header_content_html, "Device is now in NORMAL Mode."));
  }

  // restart device
  else if (readRequest.indexOf("/restart") != -1) {
    client.print(buildHeader(200, http_header_content_html, "Device is now restarting."));
    /*if (output_state == true) {
      lvl = 0;
      smooth_hsv(hue, sat, lvl);
    }*/
    delay(1000); // time for response
    ESP.restart();
  }

  // print out everything
  else if (readRequest.indexOf("/log") != -1) {
    bool output_state_temp = EEPROM.read(ADDR_STATE);
    uint16_t hue_temp = EEPROM.read(ADDR_HUE);
    uint16_t sat_temp = EEPROM.read(ADDR_SAT);
    uint16_t lvl_temp = EEPROM.read(ADDR_LVL);

    String resp ="{";
      resp += "\"Lamp\":{";
        resp += "\"state\":" + String(output_state_temp) + ",\"hue\":" + String(hue_temp) + ",\"saturation\":" + String(sat_temp) + ",\"brightness\":" + String(lvl_temp) + ",";
        resp += "\"expected\":{";
          resp += "\"State\":" + String(output_state) + ",\"hue\":" + String(hue) + ",\"saturation\":" + String(sat) + ",\"brightness\":" + String(lvl);
        resp += "},";
        resp += "\"i_hue\":" + String(i_hue) + ",\"i_sat\":" + String(i_sat) + ",\"i_lvl\":" + String(i_lvl) + ",";
        resp += "\"R\":" + String(r_value) + ",\"G\":" + String(g_value) + ",\"B\":" + String(b_value);
      resp += "},";
      resp += "\"OTA\":" + String(otaFlag);
    resp += "}";

    client.print(buildHeader(200, http_header_content_json, resp));
  }

  // request does not match any of the locations
  else {client.print(buildHeader(404, http_header_content_html, page_not_found));}

  client.flush();
  client.stop(); // stops request and throws error in browser
}


/*
* update builtin led for connectivity status
* 1st - wifi connection            -> led is on
* 2nd - wifi connection & ota mode -> led blinking
* 3rd - no wifi connection         -> led is off
*/
bool wifi_status() {
  bool wifi_status = false;

  if (!(WiFi.status() != WL_CONNECTED) && !otaFlag) { //while (WiFi.waitForConnectResult() != WL_CONNECTED) // reconnect if lost
    digitalWrite(BUILTIN_LED, LOW); // inverted - led on
    wifi_status = true;
  }
  else if (!(WiFi.status() != WL_CONNECTED) && otaFlag) {
    digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED)); // toggle
    delay(ota_led_interval);
    wifi_status = true;
  }
  else {digitalWrite(BUILTIN_LED, HIGH);} // inverted - led off

  return wifi_status;
}


/*
* immediately switch off all lights
*/
void all_off() {
  output_state = false;
  lvl = 0;
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
  EEPROM.put(ADDR_STATE, output_state);
  EEPROM.put(ADDR_HUE, hue);
  EEPROM.put(ADDR_SAT, sat);
  EEPROM.put(ADDR_LVL, lvl);
  EEPROM.commit(); // only commit
  //EEPROM.end(); // commit + release RAM of content
}


/*
* check whether physical switch is toggled or not
* does not indicate the same status twice in a row
*/
void phys_switch() {
  phys_io_switch = digitalRead(D_INPUT);

  if (phys_io_switch != _phys_io_switch) {
    if (phys_io_switch == HIGH) {
      lvl = 0;
    } else {
      lvl = 100;
    }
    smooth_hsv(hue, sat, lvl);
  }

  _phys_io_switch = phys_io_switch; // new before state
}


/*
* toggles OTA mode - via button on board
*/
void ota_toggle() {
  ota_io_button = digitalRead(OTA_BUTTON);

  if (ota_io_button != _ota_io_button && ota_io_button == 1) {
    otaFlag = !otaFlag; // toggle when input changed
    if (otaFlag) {OTA();} // start OTA service
  }

  if (WiFi.status() != WL_CONNECTED) {otaFlag = false;} // disable OTA when not connected to wifi

  _ota_io_button = ota_io_button; // new before state
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
* handle incoming mqtt message
*/
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  /*Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }

  Serial.println();

  if ((char)payload[0] == '1') {
    digitalWrite(BUILTIN_LED, LOW);
  } else {
    digitalWrite(BUILTIN_LED, HIGH);
  }*/
  return;
}


/*
* Smooth transition to new color, saturation or brightness
* direction:
* 2 - down; counter-clockwise
* 1 - up; clockwise
* 0 - not changed; do nothing
*/
bool smooth_hsv(int _hue, int _sat, int _lvl) {
  uint8_t hue_direction, sat_direction, lvl_direction; // tmp direction stores

  // check directions
  uint8_t temp_hue = (i_hue - _hue + 360) % 360; // modulo operation (start - destination + 360) mod 360)

  if (i_hue == _hue) {hue_direction = 0;}
  else if (temp_hue <= 180) {hue_direction = 2;} // counter-clockwise
  else if (temp_hue  > 180) {hue_direction = 1;} // clockwise

  if (i_sat > _sat) {sat_direction = 2;} // down
  else if (i_sat < _sat) {sat_direction = 1;} // up
  else {sat_direction = 0;}

  if (i_lvl > _lvl) {lvl_direction = 2;} // down
  else if (i_lvl < _lvl) {lvl_direction = 1;} // up
  else {lvl_direction = 0;}

  // smooth brightness change loop
  // continue until every direction is set to "not changed"
  while (!(hue_direction == 0 && sat_direction == 0 && lvl_direction == 0)) {
    // decrease or increase
    if (hue_direction != 0) {
      if (hue_direction == 1) {
        if (i_hue == 360 && _hue != 360) {i_hue = -1;} // -1; otherwise it could result in a loop
        i_hue++;
      }
      else if (hue_direction == 2) {
        if (i_hue == 0 && _hue != 0) {i_hue = 361;} // 361; otherwise it could result in a loop
        i_hue--;
      }
    }

    if (sat_direction != 0) {
      if (sat_direction == 1) {i_sat++;}
      else if (sat_direction == 2) {i_sat--;}
    }

    if (lvl_direction != 0) {
      if (lvl_direction == 1) {i_lvl++;}
      else if (lvl_direction == 2) {i_lvl--;}
    }

    // reached destination - set direction to "not changed"
    if (i_hue == _hue) {hue_direction = 0;}
    if (i_sat == _sat) {sat_direction = 0;}
    if (i_lvl == _lvl) {lvl_direction = 0;}

    float curve_lvl = pow(2, (i_lvl / r)) - 1; // dimming curve calculation

    // convert to rgb & output with a delay between changes
    hsv2rgb(i_hue, i_sat, curve_lvl);
    set_value(r_value, g_value, b_value);
    delay(fade_delay);
  }

  // save new before state
  hue = i_hue;
  sat = i_sat;
  lvl = i_lvl;

  if (lvl == 0) {output_state = false;} // set global lamp io state
  else {output_state = true;}

  write_to_eeprom(); // write after smooth transition

  return output_state;
}


/*
* builds a response with parameters
*/
String buildHeader(
  int http_header_code,
  String http_header_content_type,
  String http_body
) {
  String http_header_description;
  String http_body_content;
  String http_header_connection = "Connection: close";

  switch(http_header_code) {
    case 200: http_header_description = "OK"; break;
    case 204: http_header_description = "No Content"; break;
    case 301: http_header_description = "Moved Permanently"; break;
    case 400: http_header_description = "Bad Request"; break;
    case 403: http_header_description = "Forbidden"; break;
    case 404: http_header_description = "Not Found"; break;
    default:  http_header_description = ""; break;
  }

  if (http_header_content_type == http_header_content_html && http_header_code != 204) {
    http_body_content = "<!DOCTYPE html>" + newLine;
    http_body_content += "<html>" + newLine;
    http_body_content += http_body + newLine;
    http_body_content += "</html>" + newLine;
  } else {
    http_body_content = http_body;
  }

  String http_response = "HTTP/1.1" + String(' ') + String(http_header_code) + String(' ') + http_header_description + newLine;
  http_response += "Content-Length: " + String(http_body_content.length()) + newLine;
  http_response += http_header_content_type + newLine;
  http_response += http_header_connection + newLine + newLine;
  http_response += http_body_content;

  return http_response;
}


/*
* OTA - Over the air update
* handles ota call
* restart device after update
*/
void OTA() {
  Serial.println("OTA enabled");
  /*ArduinoOTA.setPort(ota_port);
  ArduinoOTA.setHostname(ota_name);
  ArduinoOTA.setPassword(ota_passwd); // (const char *)"password123"*/

  ArduinoOTA.onStart([]() {smooth_hsv(hue, sat, 0);});

  ArduinoOTA.onEnd([]() {
    all_off();
    otaFlag = false;
    Serial.flush();
    ESP.restart();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    all_off();
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    /*hsv2rgb(hue, sat, (progress/(total/100))); // brightness goes up with percentage
    set_value(r_value, g_value, b_value);*/
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
  float f, p, q, t, _r_value, _g_value, _b_value;

  if (h == 360) {h = 0;} // there is no case for i = 6
  /*else if (h <= 240 && h >= 120) {
    h = 360 - (h - 240);
  }
  else if (h <= 0 && h <= 120) {
    h = 360 + (h - 120);
  }*/

  h /= 360; // hue
  s /= 100; // saturation
  v /= 100; // value or brightness

  if (s == 0) {
    _r_value = _g_value = _b_value = v; // achromatic (grey)
  } else {
    i = floor(h * 6);
    f = h * 6 - i;
    p = v * (1 - s);
    q = v * (1 - f * s);
    t = v * (1 - (1 - f) * s);

    switch(i) {
      case 0:
        _r_value = v;
        _g_value = t;
        _b_value = p;
        break;
      case 1:
        _r_value = q;
        _g_value = v;
        _b_value = p;
        break;
      case 2:
        _r_value = p;
        _g_value = v;
        _b_value = t;
        break;
      case 3:
        _r_value = p;
        _g_value = q;
        _b_value = v;
        break;
      case 4:
        _r_value = t;
        _g_value = p;
        _b_value = v;
        break;
      case 5:
        _r_value = v;
        _g_value = p;
        _b_value = q;
        break;
      }
    }

    r_value = round(_r_value * PWMRANGE);
    g_value = round(_g_value * PWMRANGE);
    b_value = round(_b_value * PWMRANGE);
}
