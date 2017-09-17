/*
* https://github.com/esp8266/Arduino/blob/master/libraries/ESP8266WiFi/examples/WiFiWebServer/WiFiWebServer.ino
* 
* Homebridge:
* http://server_ip/off
* http://server_ip/on
* http://server_ip/io_status/
* http://server_ip/lvl_status/
* http://server_ip/lvl/
* http://server_ip/hue/
* http://server_ip/hue_status/
* http://server_ip/sat/
* http://server_ip/sat_status/
* 
* OTA Update
* http://server_ip/ota/
* 
* To upload a new version of the sketch, browse http://server_ip/ota/ first...
* this will set the ESP8266 in OTA Mode
*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h> //for OTA
#include <WiFiUdp.h> //for OTA
#include <ArduinoOTA.h> //for OTA

//global
#define red 15 //D8 - Red channel
#define green 12 //D6 - Green channel
#define blue 13 //D7 - Blue channel
#define d_input 5 //D1 - Physical Switch

#define PWMRANGE 1023 //ESP8266 -> maximum brightness

const char* ssid = "";
const char* password = "";

int state; //current state
int _delay; //delay between brightness, hue & saturation steps
int r, g, b; //output values

//brightness
int lvl; //brightness 0-100
int i_lvl;
int lvl_direction;

//color
int hue; //color 0-359
int i_hue;
int hue_direction; //clockwise, counter-clockwise
int sat; //saturation 0-100
int i_sat;
int sat_direction;

//physical
int _io; //last io state
int io; //new io state

bool otaFlag = false;

String readString; //request variable

WiFiServer server(80); //server instance; listen port 80

int WiFiStart() {
  /*IPAddress ip(192,168,1,10); //fixed IP
  IPAddress gateway(192,168,1,1); //gateway (router) IP
  IPAddress subnet(255,255,255,0); //subnet mask*/

  //WiFi.config(ip, gateway, subnet);
  WiFi.mode(WIFI_STA); //configure as wifi station
  WiFi.begin(ssid, password); //connect to network

  //wait for connection result
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(5000);
    ESP.restart();
  }

  //wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  //Serial.println(WiFi.localIP()); //print IP address
  server.begin(); //start server
}

int all_off() {
  state = 0;
  set_value(0, 0, 0);
}

int set_value(int set_r, int set_g, int set_b) {
  analogWrite(red, set_r);
  analogWrite(green, set_g);
  analogWrite(blue, set_b);
}

int smooth_hsv(int _state, int _hue, int _sat, int _lvl) {
  int temp_hue;
  int temp_sat;
  int temp_lvl;
  
  if (_state == 0) { //off
    i_hue = 0;
    i_sat = 100;
    i_lvl = 0;
  }

  if (i_hue > _hue) {temp_hue = i_hue - _hue;}
  else if (i_hue < _hue) {temp_hue = _hue - i_hue;}

  if (i_hue == _hue) {
    hue_direction = 2;
    temp_hue = 0;
  }
  else if (temp_hue > 180) {hue_direction = 0;} //counter-clockwise
  else if (temp_hue < 180) {hue_direction = 1;} //clockwise

  if (i_sat > _sat) {
    sat_direction = 0; //down
    temp_sat = i_sat - _sat;
  }
  else if (i_sat < _sat) {
    sat_direction = 1; //up
    temp_sat = _sat - i_sat;
  }
  else {
    sat_direction = 2; //not changed
    temp_sat = 0;
  }

  if (i_lvl > _lvl) {
    lvl_direction = 0; //down
    temp_lvl = i_lvl - _lvl;
  }
  else if (i_lvl < _lvl) {
    lvl_direction = 1; //up
    temp_lvl = _lvl - i_lvl;
  }
  else {
    lvl_direction = 2; //not changed
    temp_lvl = 0;
  }

  int i_max; //largest difference
  if (temp_hue == 0 && temp_sat == 0 && temp_lvl == 0) {i_max = -1;}
  else {
    if (temp_hue >= temp_sat) {i_max = temp_hue;} else {i_max = temp_sat;}
    if (i_max >= temp_lvl) {i_max = i_max;} else {i_max = temp_lvl;}
  }

  for (int i_end = 0; i_end < i_max; i_end++) {
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

    hsv2rgb(i_hue, i_sat, i_lvl);
    set_value(r, g, b);
    
    delay(_delay);

    //reached maximum?
    if (i_lvl == _lvl) {lvl_direction = 2;}
    if (i_hue == _hue) {hue_direction = 2;}
    if (i_sat == _sat) {sat_direction = 2;}
  }

  _lvl = i_lvl;
  _hue = i_hue;
  _sat = i_sat;
}

int phys_switch() {
  io = digitalRead(d_input);
  
  if (io != _io) {
    if (io == HIGH) {
      smooth_hsv(state, hue, sat, 0);
      state = 0;
    } else {
      smooth_hsv(state, hue, sat, 100);
      state = 1;
    }
  }
  
  _io = io;
  delay(_delay);
}

void hsv2rgb(float h, float s, float v) {
  int i;
  float f, p, q, t, _r, _g, _b;
  
  h /= 360;
  s /= 100;
  v /= 100;
  
  if (s == 0) { //Achromatic (grey)
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

    r = round(_r*PWMRANGE);
    g = round(_g*PWMRANGE);
    b = round(_b*PWMRANGE);
}

int OTA() {
  Serial.println("OTA");
  
  /*ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname("ESP8266");
  ArduinoOTA.setPassword((const char *)"123");*/
  
  ArduinoOTA.onStart([]() {
    all_off();
  });
  ArduinoOTA.onEnd([]() {
    all_off();
    otaFlag = false;
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

void setup() {
  state = 0;

  //brightness
  lvl = 0;
  _delay = 5;

  //color
  hue = 0;
  sat = 100;

  //physical
  _io = 0;
  io = 0;

  Serial.begin(115200);
  pinMode(red, OUTPUT);
  pinMode(green, OUTPUT);
  pinMode(blue, OUTPUT);
  pinMode(d_input, INPUT_PULLUP);

  WiFiStart();
  all_off();
}

void loop() {
  if (otaFlag) {
    ArduinoOTA.handle();
  } else {
    phys_switch();
    
    if (WiFi.status() != WL_CONNECTED) {WiFiStart();} //reconnect if lost
    WiFiClient client = server.available(); //if client connected
    
    if (!client) {return;} //no client; restart loop
    while (!client.available()) {delay(1);} //wait until client send data
  
    if (client) { //client found
      while (client.connected()) {
        
        char c = client.read();
        if (readString.length() < 100) {
          readString += c;
        }
        //readString = client.readStringUntil('\r');
  
        if (c == '\n') {
          //prepare response
          /*client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println();
          String response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\n";*/
          String response;
          response += "HTTP/1.1 200 OK\r\n";
          response += "Content-Type: text/html\r\n\r\n";
  
          if (readString.indexOf("/off") != -1) {
            if (state == 1) {
              smooth_hsv(state, hue, sat, 0);
              state = 0;
            }
            response += state;
          }
  
          if (readString.indexOf("/on") > 0) {
            if (lvl == 0) {lvl = PWMRANGE;}
            smooth_hsv(state, hue, sat, lvl);
            state = 1;
            response += state;
          }
  
          if (readString.indexOf("/lvl/") != -1) {
            char charBuf[50];
            readString.toCharArray(charBuf, 50);
            lvl = atoi(strtok(charBuf, "GET /lvl/"));
            smooth_hsv(state, hue, sat, lvl);

            if (lvl != 0) {state = 1;}
            else {state = 0;}
            
            response += lvl;
          }

          if (readString.indexOf("/hue/") != -1) {
            char charBuf_hue[50];
            readString.toCharArray(charBuf_hue, 50);
            hue = atoi(strtok(charBuf_hue, "GET /hue/"));
            smooth_hsv(state, hue, sat, lvl);
            response += hue;
          }

          if (readString.indexOf("/sat/") != -1) {
            char charBuf_sat[50];
            readString.toCharArray(charBuf_sat, 50);
            sat = atoi(strtok(charBuf_sat, "GET /sat/"));
            smooth_hsv(state, hue, sat, lvl);
            response += sat;
          }
  
          if (readString.indexOf("/io_status/") != -1) {response += state;}
          if (readString.indexOf("/lvl_status/") != -1) {response += lvl;}
          if (readString.indexOf("/hue_status/") != -1) {response += hue;}
          if (readString.indexOf("/sat_status/") != -1) {response += sat;}

          if (readString.indexOf("/ota/") != -1) {
            OTA();
            otaFlag = true;

            response += "<!DOCTYPE HTML>\r\n";
            response += "<html>\r\n";
            response += "ESP8266 is now in OTA Mode. In this mode you can upload new firmware to the device.";
            response += "</html>\n";
          }
          
          client.print(response);
          
          delay(1);
          
          client.flush();
          client.stop();
          
          readString = "";
        }
      }
    }
  }
}
