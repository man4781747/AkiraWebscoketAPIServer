#ifndef WEBSOCKET_API_MODEL_H
#define WEBSOCKET_API_MODEL_H

#include "AkiraWebscoketAPIServer.h"
#include <ESPAsyncWebServer.h>
#include "AsyncTCP.h"
#include <ArduinoJson.h>

void ws_Test(AsyncWebSocket *server, AsyncWebSocketClient *client, DynamicJsonDocument *D_baseInfo, DynamicJsonDocument *D_PathParameter, DynamicJsonDocument *D_QueryParameter, DynamicJsonDocument *D_FormData)
{
  client->text("HELLO~");
}

#endif