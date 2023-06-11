#ifndef WEBSOCKET_API_H
#define WEBSOCKET_API_H

#include "AkiraWebscoketAPIServer.h"
#include "websocket_models.h"

void setWebsocketAPI() {
  WebsocketAPIServer.AddWebsocketAPI("/api/HI", "GET", &ws_Test);
}

#endif