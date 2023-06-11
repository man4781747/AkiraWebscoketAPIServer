#include "AkiraWebscoketAPIServer.h"
#include <esp_log.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <SPI.h>
#include "AsyncTCP.h"
#include <NTPClient.h>
#include <ESPAsyncWebServer.h>
#include <unordered_map>
#include <map>
#include <vector>
#include <regex>
#include <TimeLib.h>
#include <ArduinoOTA.h> 
#include <HTTPClient.h>

AsyncWebServer asyncServer(80);
AsyncWebSocket ws("/ws");

const long  gmtOffset_sec = 3600*8; // GMT+8
const int   daylightOffset_sec = 0; // DST+0
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", gmtOffset_sec, daylightOffset_sec);

DynamicJsonDocument urlParamsToJSON(const std::string& urlParams) {
  std::stringstream ss(urlParams);
  std::string item;
  std::unordered_map<std::string, std::string> paramMap;
  while (std::getline(ss, item, '&')) {
    std::stringstream ss2(item);
    std::string key, value;
    std::getline(ss2, key, '=');
    std::getline(ss2, value, '=');
    paramMap[key] = value;
  }
  DynamicJsonDocument json_doc(1000);
  for (auto& it : paramMap) {
    json_doc[it.first].set(it.second);
  }
  return json_doc;
}

void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.println("WebSocket client connected");
    WebsocketAPIServer.clientNum += 1;
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.println("WebSocket client disconnected");
    WebsocketAPIServer.clientNum -= 1;
  } else if (type == WS_EVT_DATA) {
    String MessageString = String(((char *)data));
    int commaIndex = MessageString.indexOf("]");
    String METHOD = MessageString.substring(1, commaIndex);
    MessageString = MessageString.substring(commaIndex + 1);
    commaIndex = MessageString.indexOf("?");
    String Message_CMD = MessageString.substring(0, commaIndex);
    String QueryParameter = MessageString.substring(commaIndex + 1);
    DynamicJsonDocument D_QueryParameter = urlParamsToJSON(QueryParameter.c_str());
    DynamicJsonDocument D_FormData(10000);
    DeserializationError error = deserializeJson(D_FormData, D_QueryParameter["data"].as<String>());
    if (error) {
      DynamicJsonDocument D_errorbaseInfo(2000);
      // DynamicJsonDocument D_errorbaseInfo = WebsocketAPIServer.GetBaseWSReturnData("["+METHOD+"]"+Message_CMD);
      D_errorbaseInfo["message"] = "FAIL";
      D_errorbaseInfo["parameter"]["message"] = "API帶有的Data解析錯誤，參數格式錯誤?";
      String ErrorMessage;
      serializeJsonPretty(D_errorbaseInfo, ErrorMessage);
      client->text(ErrorMessage);
      D_errorbaseInfo.clear();
    }
    D_QueryParameter.remove("data");
    std::string Message_CMD_std = std::string(Message_CMD.c_str());
    std::string METHOD_std = std::string(METHOD.c_str());
    // DynamicJsonDocument D_baseInfo = Machine_Ctrl.BackendServer.GetBaseWSReturnData("["+String(METHOD_std.c_str())+"]"+String(Message_CMD_std.c_str()));
    DynamicJsonDocument D_baseInfo(2000);
    
    Serial.println(Message_CMD);
    Serial.println(METHOD);

    bool IsFind = false;
    for (auto it = WebsocketAPIServer.websocketApiSetting.rbegin(); it != WebsocketAPIServer.websocketApiSetting.rend(); ++it) {
      std::regex reg(it->first.c_str());
      std::smatch matches;
      if (std::regex_match(Message_CMD_std, matches, reg)) {
        std::map<int, String> UrlParameter;
        IsFind = true;
        if (it->second.count(METHOD_std)) {
          DynamicJsonDocument D_PathParameter(1000);
          int pathParameterIndex = -1;
          for (auto matches_it = matches.begin(); matches_it != matches.end(); ++matches_it) {
            if (pathParameterIndex == -1) {
              pathParameterIndex++;
              continue;
            }
            if ((int)(it->second[METHOD_std]->pathParameterKeyMapList.size()) <= pathParameterIndex) {
              break;
            }
            D_PathParameter[it->second[METHOD_std]->pathParameterKeyMapList[pathParameterIndex]] = String(matches_it->str().c_str());
          }
          it->second[METHOD_std]->func(server, client, &D_baseInfo, &D_PathParameter, &D_QueryParameter, &D_FormData);
        } else {
          ESP_LOGE("WebsocketServer", "API %s 並無設定 METHOD: %s", Message_CMD_std.c_str(), METHOD_std.c_str());
          D_baseInfo["message"] = "FAIL";
          D_baseInfo["parameter"]["message"] = "Not allow: "+String(METHOD_std.c_str());
          String returnString;
          serializeJsonPretty(D_baseInfo, returnString);
          client->text(returnString);
        }
        break;
      }
    }
    if (!IsFind) {
      ESP_LOGE("WebsocketServer", "找不到API: %s", Message_CMD_std.c_str());
      D_baseInfo["parameter"]["message"] = "找不到API: "+String(Message_CMD_std.c_str());
      D_baseInfo["action"]["status"] = "FAIL";
      D_baseInfo["action"]["message"] = "找不到API: "+String(Message_CMD_std.c_str());
      String returnString;
      serializeJsonPretty(D_baseInfo, returnString);
      client->text(returnString);
    }
    D_baseInfo.clear();
  }
}

bool C_API_SERVER::CreateSoftAP()
{
  ESP_LOGI("WIFI", "Create Soft AP");
  IPAddress AP_IP = IPAddress();
  IPAddress AP_gateway = IPAddress();
  IPAddress AP_subnet_mask = IPAddress();
  
  AP_IP.fromString("127.0.0.4");
  AP_gateway.fromString("192.168.0.1");
  AP_subnet_mask.fromString("255.255.255.0");
  WiFi.softAPdisconnect();
  WiFi.softAPConfig(AP_IP, AP_gateway, AP_subnet_mask);
  bool ISsuccess = WiFi.softAP("AkiraWebServer","12345678");

  ESP_LOGI("WIFI","AP IP:\t%s", WiFi.softAPIP().toString().c_str());
  ESP_LOGI("WIFI","AP MAC:\t%s", WiFi.softAPmacAddress().c_str());
  return ISsuccess;
}

String C_API_SERVER::ConnectWiFiAP(String SSID, String PW)
{
  WiFi.disconnect();
  WiFi.begin(SSID.c_str(),PW.c_str());
  
  time_t connectTimeout = now();
  while (!WiFi.isConnected() & now()-connectTimeout < 5) {
    delay(500);
  }
  if (!WiFi.isConnected()) {
    WiFi.disconnect();
    ESP_LOGE("WIFI","Cant connect to WIFI");
    return "";
  } else {
    String IP = WiFi.localIP().toString();
    int rssi = WiFi.RSSI();
    String mac_address = WiFi.macAddress();
    ESP_LOGI("WIFI","Server IP: %s", IP.c_str());
    ESP_LOGI("WIFI","Server RSSI: %d", rssi);
    ESP_LOGI("WIFI","Server MAC: %s", mac_address.c_str());
    return IP;
  }
}

//! OTA 相關

void OTAServiceTask(void* parameter) {
  ESP_LOGI("OTA", "Create OTA Service");
  ArduinoOTA.setPort(3232);
  ArduinoOTA.onStart([]() {
    Serial.println("OTA starting...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA end!");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("OTA auth failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("OTA begin failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("OTA connect failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("OTA receive failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("OTA end failed");
    }
  });
  ArduinoOTA.begin();
  ESP_LOGI("OTA", "Create OTA Done");
  for(;;){
    ArduinoOTA.handle();
    vTaskDelay(1000/portTICK_PERIOD_MS);
  }
}
void C_API_SERVER::CreateOTAService()
{
  if (Task__OTAService == NULL) {
    xTaskCreate(
      OTAServiceTask, "TASK__OTAService",
      10000, NULL, 1, &Task__OTAService
    );
  }
}

struct ota_para {
  String URL;
};


//* 依URL下載檔案並更新
void UpdateTask(void* parameter)
{
  for (;;) {
    HTTPClient http;
    ota_para ottaData = *(ota_para*)parameter;
    ESP_LOGI("URL_OTA", "URL: %s", ottaData.URL.c_str());
    // http.begin(ottaData.URL);
    // char* URL = (char*) parameter;
    // ESP_LOGI("URL_OTA", "URL: %s", URL);
    // http.begin(String(URL));
    http.begin("http://35.221.221.94:5566/static/firmware.bin");
    int http_code = http.GET();
    ESP_LOGI("OTAUpdateTask", "http_code: %d", http_code);
    if (http_code == HTTP_CODE_OK) {
      size_t size = http.getSize();
      Stream *dataStream = http.getStreamPtr();

      ESP_LOGI("OTAUpdateTask", "MALLOC: %d", size);
      uint8_t* binData = (uint8_t*)malloc(size);
      ESP_LOGI("OTAUpdateTask", "readBytes");
      dataStream->readBytes(binData, size);
      ESP_LOGI("OTAUpdateTask", "Update being");
      Update.begin(size);
      ESP_LOGI("OTAUpdateTask", "Update.write");
      size_t written = Update.write(binData, size);
      ESP_LOGI("OTAUpdateTask", "Free");
      free(binData);

      Serial.printf("written: %d\n", written);
      if (written == size) {
        if (Update.end()) {
          ESP_LOGE("OTAUpdateTask", "OTA更新成功，即將重開機");
          delay(1000);
          ESP.restart();
        } else {
          ESP_LOGE("OTAUpdateTask", "OTA更更新失敗:%s", Update.errorString());
        }
      } else {
        ESP_LOGE("OTAUpdateTask", "OTA更新失敗:檔案下載不完全");
      }
    }
    vTaskDelay(1000);
    WebsocketAPIServer.Task__URL_OTA = NULL;
    vTaskDelete(NULL);
  }
}

void ws_OTAapi(AsyncWebSocket *server, AsyncWebSocketClient *client, DynamicJsonDocument *D_baseInfo, DynamicJsonDocument *D_PathParameter, DynamicJsonDocument *D_QueryParameter, DynamicJsonDocument *D_FormData)
{
  if ((*D_QueryParameter).containsKey("url")) {
    String url = (*D_QueryParameter)["url"].as<String>();
    if (WebsocketAPIServer.Task__URL_OTA == NULL) {
      xTaskCreate(
        UpdateTask, "TASK__OTAService",
        10000, (void *)url.c_str(), 1, &(WebsocketAPIServer.Task__URL_OTA)
      );
    }
  } else {
    client->binary("");
  }
}
void C_API_SERVER::AddOtaApiOnWebsocketApi(String ApiPath)
{
  AddWebsocketAPI(ApiPath, "GET", &ws_OTAapi);
}

void C_API_SERVER::AddOtaApiOnHTTPApi(String ApiPath)
{
  asyncServer.on(ApiPath.c_str(), HTTP_GET, [] (AsyncWebServerRequest *request) {
    int paramsNr = request->params();
    bool isParaOK = false;
    for (int i = 0; i < paramsNr; i++)
    {
      AsyncWebParameter* p = request->getParam(i);
      String paraName = p->name();
      ESP_LOGI("URL_OTA", "para: %s", paraName.c_str());
      if (paraName == "url") {
        isParaOK = true;
        // String urlPath = p->value();
        ota_para ota_data;
        ota_data.URL = String(p->value().c_str());
        ESP_LOGI("URL_OTA", "urlPath: %s", ota_data.URL.c_str());
        if (WebsocketAPIServer.Task__URL_OTA == NULL) {
          xTaskCreate(
            UpdateTask, "TASK__OTAService",
            10000, &ota_data, 1, &(WebsocketAPIServer.Task__URL_OTA)
          );
        }
        break;
      }
    }
    request->send(200, "text/plain", "Hello");
  });
}


void C_API_SERVER::ServerStart()
{
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  ws.onEvent(onWebSocketEvent);
  asyncServer.addHandler(&ws);
  asyncServer.begin();
}

void C_API_SERVER::setAPIs()
{
  // asyncServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
  //   AsyncWebServerResponse* response = request->beginResponse(SPIFFS, "/web/index.html", "text/html");
  //   request->send(response);
  // });
  asyncServer.on("/api/HI", HTTP_GET, [&](AsyncWebServerRequest *request){
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", "OK");
    request->send(response);
    // SendHTTPesponse(request, response);
  });
  asyncServer.onNotFound([](AsyncWebServerRequest *request){
    request->send(404, "text/plain", "not Found!");
  });
}

void C_API_SERVER::AddWebsocketAPI(String APIPath, String METHOD, void (*func)(AsyncWebSocket*, AsyncWebSocketClient*, DynamicJsonDocument*, DynamicJsonDocument*, DynamicJsonDocument*, DynamicJsonDocument*))
{
  C_WebsocketAPI *newAPI = new C_WebsocketAPI(APIPath, METHOD, func);
  std::unordered_map<std::string, C_WebsocketAPI*> sub_map;

  if (websocketApiSetting.count(std::string(newAPI->APIPath.c_str())) == 0) {
    sub_map[std::string(newAPI->METHOD.c_str())] = newAPI;
    websocketApiSetting.insert(
      std::make_pair(std::string(newAPI->APIPath.c_str()), sub_map)
    );
  } else {
    websocketApiSetting[std::string(newAPI->APIPath.c_str())][std::string(newAPI->METHOD.c_str())] = newAPI;
  }
}

int C_API_SERVER::GetClientNum()
{
  return WebsocketAPIServer.clientNum;
}

time_t C_API_SERVER::GetTimeByNTP()
{
  timeClient.begin();
  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }
  configTime(28800, 0, "pool.ntp.org");
  return (time_t)timeClient.getEpochTime();
}

String C_API_SERVER::GetAPIP()
{
  return WiFi.localIP().toString();
}

void C_API_SERVER::AddHttpAPI(String APIPath, WebRequestMethod METHOD, ArRequestHandlerFunction onRequest)
{
  asyncServer.on(APIPath.c_str(), METHOD, onRequest);
}

C_API_SERVER WebsocketAPIServer;
