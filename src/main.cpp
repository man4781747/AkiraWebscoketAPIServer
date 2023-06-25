
//https://zhuanlan.zhihu.com/p/591800066
#include <Arduino.h>
#include <esp_log.h>
#include "AkiraWebscoketAPIServer.h"
#include "websocket_urls.h"

void setup() {
  Serial.begin(115200);
  USBSerial.begin(57600);
  Serial.println("START");

  WebsocketAPIServer.ConnectWiFiAP("GalenaBomb", "0983367632");
  setWebsocketAPI();
  WebsocketAPIServer.GetTimeByNTP();
  WebsocketAPIServer.CreateOTAService();
  WebsocketAPIServer.setAPIs();
  WebsocketAPIServer.ServerStart();
}

void loop() {
  delay(1000);
}