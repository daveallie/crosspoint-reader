#pragma once
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

#include <memory>

class UploadServer {
  bool running = false;
  std::unique_ptr<DNSServer> dnsServer = nullptr;
  std::unique_ptr<AsyncWebServer> server = nullptr;
  std::function<void(AsyncWebServerRequest* request, const String& filename)> onStart;
  std::function<void(AsyncWebServerRequest* request, const uint8_t* data, size_t len)> onPart;
  std::function<void(AsyncWebServerRequest* request)> onEnd;

 public:
  UploadServer(const std::function<void(AsyncWebServerRequest* request, const String& filename)>& onStart,
               const std::function<void(AsyncWebServerRequest* request, const uint8_t* data, size_t len)>& onPart,
               const std::function<void(AsyncWebServerRequest* request)>& onEnd)
      : onStart(onStart), onPart(onPart), onEnd(onEnd) {}
  ~UploadServer() = default;
  void begin();
  void loop();
  void end();
};
