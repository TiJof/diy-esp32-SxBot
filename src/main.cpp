#include <Arduino.h>

// Include the AccelStepper library:
#include <AccelStepper.h>

// SpeedStepper
#define SpeedPinDir 13
#define SpeedPinStep 12
#define SpeedMotorInterfaceType 1
// Create a new instance of the AccelStepper class:
AccelStepper SpeedStepper = AccelStepper(SpeedMotorInterfaceType, SpeedPinStep, SpeedPinDir);
int AccelStepperMaxSpeed = 1000;

// DepthStepper
#define DepthPinDir 21
#define DepthPinStep 19
#define DepthMotorInterfaceType 1
// Create a new instance of the AccelStepper class:
AccelStepper DepthStepper = AccelStepper(DepthMotorInterfaceType, DepthPinStep, DepthPinDir);
int DepthStepperMaxSpeed = 1500;

// Include the CaptivePortal to set custom SSID/Passpgrase
#include <WiFiManager.h>
#define TRIGGER_PIN 34
WiFiManager wm;
#include <ESPmDNS.h>

// WebServer and WebSocket
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
// Create a WebSocket object
AsyncWebSocket ws("/ws");

String message = "";
String sliderValue1 = "0";
String sliderValue2 = "0";
int Speed;
int Depth;

// Json Variable to Hold Slider Values
JSONVar sliderValues;

// Get Slider Values
String getSliderValues()
{
  sliderValues["sliderValue1"] = String(sliderValue1);
  sliderValues["sliderValue2"] = String(sliderValue2);

  String jsonString = JSON.stringify(sliderValues);
  return jsonString;
}

// Initialize SPIFFS
void initFS()
{
  if (!SPIFFS.begin())
  {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  else
  {
    Serial.println("SPIFFS mounted successfully");
  }
}

void notifyClients(String sliderValues)
{
  ws.textAll(sliderValues);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    message = (char *)data;
    if (message.indexOf("1s") >= 0)
    {
      sliderValue1 = message.substring(2);
      Speed = map(sliderValue1.toInt(), 0, 100, 0, AccelStepperMaxSpeed);
      Serial.println(Speed);
      Serial.print(getSliderValues());
      notifyClients(getSliderValues());
    }
    if (message.indexOf("2s") >= 0)
    {
      sliderValue2 = message.substring(2);
      Depth = map(sliderValue2.toInt(), 0, 100, 0, DepthStepperMaxSpeed);
      Serial.println(Depth);
      Serial.print(getSliderValues());
      notifyClients(getSliderValues());
    }
    if (strcmp((char *)data, "getValues") == 0)
    {
      notifyClients(getSliderValues());
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

void initWebSocket()
{
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void checkButton()
{
  // check for button press
  if (digitalRead(TRIGGER_PIN) == HIGH)
  {
    // poor mans debounce/press-hold, code not ideal for production
    delay(50);
    if (digitalRead(TRIGGER_PIN) == HIGH)
    {
      Serial.println("Button Pressed");
      // still holding button for 3000 ms, reset settings, code not ideal for production
      delay(3000); // reset delay hold
      if (digitalRead(TRIGGER_PIN) == HIGH)
      {
        Serial.println("Button Held");
        Serial.println("Erasing Config, restarting");
        wm.resetSettings();
        ESP.restart();
      }

      // start portal w delay
      Serial.println("Starting config portal");
      wm.setConfigPortalTimeout(120);

      if (!wm.startConfigPortal("SxBot"))
      {
        Serial.println("failed to connect or hit timeout");
        delay(3000);
        // ESP.restart();
      }
      else
      {
        // if you get here you have connected to the WiFi
        Serial.println("connected...yeey :)");
      }
    }
  }
}

void setup()
{
  // Some debug
  Serial.begin(115200);
  Serial.println("Initializing");

  // GPIO TRIGGER_PIN button, 2 action on it to clear Wi-Fi settings
  pinMode(TRIGGER_PIN, INPUT);

  // set dark theme
  wm.setClass("invert");

  // Automatically connect using saved credentials,
  // if connection fails, it starts an access point with the specified name ( "AutoConnectAP"),
  // if empty will auto generate SSID, if password is blank it will be anonymous AP (wm.autoConnect())
  // then goes into a blocking loop awaiting configuration and will return success result

  bool res;
  wm.setHostname("sxbot");
  res = wm.autoConnect("SxBot"); // anonymous ap

  if (!res)
  {
    Serial.println("Failed to connect");
    // ESP.restart();
  }
  else
  {
    // if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");
  }

  if (!MDNS.begin("sxbot"))
  {
    Serial.println("Error setting up MDNS responder!");
    while (1)
    {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");

  // Set the maximum speed in steps per second
  SpeedStepper.setMaxSpeed(AccelStepperMaxSpeed);
  DepthStepper.setMaxSpeed(DepthStepperMaxSpeed);
  // Set the current position to 0
  SpeedStepper.setCurrentPosition(0);
  DepthStepper.setCurrentPosition(0);

  MDNS.addService("http", "tcp", 80);

  initFS();

  initWebSocket();

  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/index.html", "text/html"); });

  server.serveStatic("/", SPIFFS, "/");

  // Start server
  server.begin();

  Serial.println("let's go");
}

void loop()
{
  checkButton();
  SpeedStepper.setSpeed(Speed);
  SpeedStepper.run();

  DepthStepper.moveTo(Depth);
  DepthStepper.setSpeed(10);
  DepthStepper.runSpeedToPosition();

  // if button STOP
  // SpeedStepper.stop();

  // if button random
  // stepper.moveTo(rand() % 200);
  // stepper.setMaxSpeed((rand() % 200) + 1);
  // stepper.setAcceleration((rand() % 200) + 1);
  ws.cleanupClients();
}