#ifndef WIFI_CTRLER_H
#define WIFI_CTRLER_H


#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h> 
#include "AsyncTCP.h"
#include <unordered_map>
#include <map>
#include <vector>
#include <regex>

extern AsyncWebServer asyncServer;
extern AsyncWebSocket ws;

class C_WebsocketAPI
{
  public:
    String APIPath, METHOD;
    std::vector<String> pathParameterKeyMapList;
    /**
     * @brief 
     * ws, client, returnInfoJSON, PathParameterJSON, QueryParameterJSON, FormDataJSON
     * 
     */
    void (*func)(
      AsyncWebSocket*, AsyncWebSocketClient*, 
      DynamicJsonDocument*, DynamicJsonDocument*, DynamicJsonDocument*, DynamicJsonDocument*
    );
    C_WebsocketAPI(
      String APIPath_, String METHOD_, 
      void (*func_)(
        AsyncWebSocket*, AsyncWebSocketClient*, 
        DynamicJsonDocument*, DynamicJsonDocument*, DynamicJsonDocument*, DynamicJsonDocument*
      )
    ) {
      METHOD = METHOD_;
      func = func_;
      std::regex ms_PathParameterKeyRegex(".*\\(\\<([a-zA-Z0-9_]*)\\>.*\\).*");
      std::regex ms_PathParameterReplace("\\<.*\\>");  
      std::smatch matches;
      char *token;
      char newAPIPathChar[APIPath_.length()+1];
      strcpy(newAPIPathChar, APIPath_.c_str());
      token = strtok(newAPIPathChar, "/");
      std::string reMix_APIPath = "";
      while( token != NULL ) {
        std::string newMSstring = std::string(token);
        if (std::regex_search(newMSstring, matches, ms_PathParameterKeyRegex)) {
          pathParameterKeyMapList.push_back(String(matches[1].str().c_str()));
          newMSstring = std::regex_replace(matches[0].str().c_str(), ms_PathParameterReplace, "");
        }
        if (newMSstring.length() != 0) {
          reMix_APIPath += "/" + newMSstring;
        }
        token = strtok(NULL, "/");
      }
      APIPath = String(reMix_APIPath.c_str());
      ESP_LOGI("WS_API Setting", "註冊 API: [%s]%s", METHOD.c_str(), reMix_APIPath.c_str());
    };
  private:
};

/**
 * @brief 儀器WIFI設定
 * 
 */
class C_API_SERVER
{
  public:
    C_API_SERVER(void){
      WiFi.mode(WIFI_AP_STA);
    };
    int clientNum = 0;
    //? 建立IDE的OTA服務
    void CreateOTAService();
    //? 建立LocalAP服務
    bool CreateSoftAP();
    //? 與WIFI基地台連線
    String ConnectWiFiAP(String SSID, String PW="");

    //? API後台服務建立
    void ServerStart();

    //? 建立基礎API
    void setAPIs();

    //? 獲得Websocket客戶端數量
    int GetClientNum();

    //? 獲得網路時間
    time_t GetTimeByNTP();

    //? 核心 - 幫伺服器新增一項websocket的API
    void AddWebsocketAPI(String APIPath, String METHOD, void (*func)(AsyncWebSocket*, AsyncWebSocketClient*, DynamicJsonDocument*, DynamicJsonDocument*, DynamicJsonDocument*, DynamicJsonDocument*));
    
    //? websocket的API列表
    std::map<std::string, std::unordered_map<std::string, C_WebsocketAPI*>> websocketApiSetting;

  private:
};
extern C_API_SERVER WebsocketAPIServer;
extern AsyncWebServer asyncServer;
extern AsyncWebSocket ws;
#endif