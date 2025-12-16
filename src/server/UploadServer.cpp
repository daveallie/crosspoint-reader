#include "UploadServer.h"

#include <WiFi.h>

#include "html/UploadHtml.generated.h"
#include "html/UploadSuccessHtml.generated.h"

void UploadServer::begin() {
  dnsServer.reset(new DNSServer());
  server.reset(new AsyncWebServer(80));

  WiFi.mode(WIFI_AP);
  WiFi.softAP("CrossPoint");

  server->on("/upload", HTTP_GET, [](AsyncWebServerRequest* request) { request->send(200, "text/html", UploadHtml); });

  server->on(
      "/upload", HTTP_POST, [](AsyncWebServerRequest* request) { request->send(200, "text/html", UploadSuccessHtml); },
      [this](AsyncWebServerRequest* request, const String& filename, const size_t index, const uint8_t* data,
             const size_t len, const bool final) {
        // This function is called multiple times as data chunks are received
        if (!index) {
          onStart(request, filename);
        }

        if (len) {
          onPart(request, data, len);
        }

        if (final) {
          onEnd(request);
        }
      });

  server->onNotFound([](AsyncWebServerRequest* request) { request->redirect("/upload"); });

  dnsServer->start(53, "*", WiFi.softAPIP());
  server->begin();
  running = true;
}

void UploadServer::loop() {
  if (running) {
    dnsServer->processNextRequest();
  }
}

void UploadServer::end() {
  if (running) {
    server->reset();
    server->end();
    dnsServer->stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    server.reset();
    dnsServer.reset();
    running = false;
  }
}
