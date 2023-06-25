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
    ESP_LOGI("WebSocket API Server", "WebSocket client connected");
    WebsocketAPIServer.clientNum += 1;
  } else if (type == WS_EVT_DISCONNECT) {
    ESP_LOGI("WebSocket API Server", "WebSocket client disconnected");
    WebsocketAPIServer.clientNum -= 1;
  } else if (type == WS_EVT_DATA) {
    ESP_LOGV("WebSocket API Server", "收到訊息");
    vTaskDelay(10/portTICK_PERIOD_MS);
    // String MessageString= String(remove_non_utf8(std::string((char *)data)).c_str());
    String MessageString = String((char *)data);

    //! 奇怪的現象 - 伺服器端在接收網頁端的websocket訊息時，可能會收到超出字串長度的亂碼，需要將其排除掉
    //TODO 原因尚不明
    MessageString = MessageString.substring(0, len);

    int commaIndex = MessageString.indexOf("]");
    String METHOD = MessageString.substring(1, commaIndex);

    //! 當POST與PATCH長度太長時，WebSocket可能會沒法一次全收完訊息，會導致後續錯誤
    //! 所以必須指認結尾符號 '\n'
    bool dataFail = false;
    if (METHOD == "PATCH" or METHOD == "POST") {
      int paraLen = MessageString.length();
      if (paraLen == 0) {
        dataFail = true;
      } else {
        char lastChar = MessageString.charAt(paraLen-1);
        if (lastChar != '\n') {
          dataFail = true;
        } else {
          MessageString = MessageString.substring(commaIndex + 1, paraLen);
        }
      }
    } else {
      MessageString = MessageString.substring(commaIndex + 1);
    }

    commaIndex = MessageString.indexOf("?");
    String Message_CMD = MessageString.substring(0, commaIndex);
    String QueryParameter = MessageString.substring(commaIndex + 1);
    DynamicJsonDocument D_QueryParameter = urlParamsToJSON(QueryParameter.c_str());
    DynamicJsonDocument D_FormData(10000);
    String dataString = D_QueryParameter["data"].as<String>();
    DeserializationError error = deserializeJson(D_FormData, dataString);

    if (error or dataFail) {
      DynamicJsonDocument D_errorbaseInfo(1000);
      D_errorbaseInfo["message"] = "FAIL";
      D_errorbaseInfo["parameter"]["message"] = "API帶有的Data解析錯誤，參數格式錯誤?";
      String ErrorMessage;
      serializeJsonPretty(D_errorbaseInfo, ErrorMessage);
      client->binary("");
      D_errorbaseInfo.clear();
    }
    else {
      D_QueryParameter.remove("data");
      std::string Message_CMD_std = std::string(Message_CMD.c_str());
      std::string METHOD_std = std::string(METHOD.c_str());
      DynamicJsonDocument D_baseInfo(10000);
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
            DynamicJsonDocument D_errorbaseInfo(1000);
            ESP_LOGE("WebSocket API Server", "API %s 並無設定 METHOD: %s", Message_CMD_std.c_str(), METHOD_std.c_str());
            D_errorbaseInfo["message"] = "FAIL";
            D_errorbaseInfo["parameter"]["message"] = "Not allow: "+String(METHOD_std.c_str());
            String returnString;
            serializeJsonPretty(D_errorbaseInfo, returnString);
            client->binary(returnString);
            D_errorbaseInfo.clear();
          }
          break;
        }
      }
      if (!IsFind) {
        DynamicJsonDocument D_errorbaseInfo(1000);
        ESP_LOGE("WebSocket API Server", "找不到API: %s", Message_CMD_std.c_str());
        D_errorbaseInfo["parameter"]["message"] = "找不到API: "+String(Message_CMD_std.c_str());
        D_errorbaseInfo["action"]["status"] = "FAIL";
        D_errorbaseInfo["action"]["message"] = "找不到API: "+String(Message_CMD_std.c_str());
        String returnString;
        serializeJsonPretty(D_errorbaseInfo, returnString);
        client->binary(returnString);
        D_errorbaseInfo.clear();
      }
      D_baseInfo.clear();
    }
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

void OTAServiceTask(void* parameter) {
  ArduinoOTA.setPort(3232);
  ArduinoOTA.onStart([]() {
    ESP_LOGI("OTA", "OTA starting...");
  });
  ArduinoOTA.onEnd([]() {
    ESP_LOGI("OTA", "OTA end!");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    ESP_LOGI("OTA", "OTA progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    ESP_LOGE("OTA", "OTA error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      ESP_LOGE("OTA", "OTA auth failed");
    } else if (error == OTA_BEGIN_ERROR) {
      ESP_LOGE("OTA", "OTA begin failed");
    } else if (error == OTA_CONNECT_ERROR) {
      ESP_LOGE("OTA", "OTA connect failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      ESP_LOGE("OTA", "OTA receive failed");
    } else if (error == OTA_END_ERROR) {
      ESP_LOGE("OTA", "OTA end failed");
    }
  });
  ArduinoOTA.begin();
  for(;;){
    ArduinoOTA.handle();
    vTaskDelay(1000/portTICK_PERIOD_MS);
  }
}

void C_API_SERVER::CreateOTAService() {
  xTaskCreate(
    OTAServiceTask, "TASK__OTAService",
    10000, NULL, 1, NULL
  );
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
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", "{\"Message\":\"HI\"}");
    request->send(response);
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

C_API_SERVER WebsocketAPIServer;
