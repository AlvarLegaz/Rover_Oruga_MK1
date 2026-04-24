#ifndef WEB_SERVER_ROVER_H
#define WEB_SERVER_ROVER_H

#include <WebServer.h>
#include <WiFi.h>

// Objetos externos compartidos
extern WebServer server;
extern bool cameraSupported;

// Inicialización del servidor
void initWebServerResources();
void setupWebServer();

// Rutas HTTP
void handleInfo();
void handleTelemetry();

void handleCapture();
void handleStream();
void handleStreamLow();

void handleConfig();
void handleSave();



#endif