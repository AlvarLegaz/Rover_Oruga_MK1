#ifndef WEB_SERVER_ROVER_H
#define WEB_SERVER_ROVER_H

#include <WebServer.h>

// Esto le dice a todos los archivos: "Hay una variable booleana llamada cameraSupported en otro lado"
extern bool cameraSupported; 
extern WebServer server;

void setupWebServer();
void handleCapture();
void handleTelemetry();
void handleHealth();

#endif