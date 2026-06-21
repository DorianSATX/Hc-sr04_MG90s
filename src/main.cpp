#include <Arduino.h>
#include <TelnetSpy.h>
TelnetSpy SerialAndTelnet;
#define Serial SerialAndTelnet  // redirect all Serial calls to TelnetSpy
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Servo.h>
#include <NewPing.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include "secrets.h"
#include <TelnetSpy.h>

// --- FORWARD DECLARATIONS ---
int getStableDistance();
void reconnect();
void handleState0(unsigned long currentTime, int distance, bool updateDisplayNow);
void handleState1(unsigned long currentTime, bool updateDisplayNow);
void handleState2(unsigned long currentTime, bool updateDisplayNow);

// --- WIFI & MQTT ---
const char* ssid        = SECRET_SSID;
const char* password    = SECRET_PASS;
const char* mqtt_server = SECRET_MQTT_SERVER;
const char* mqtt_user   = SECRET_MQTT_USER;
const char* mqtt_pass   = SECRET_MQTT_PASS;

WiFiClient   espClient;
PubSubClient client(espClient);

// --- PIN DEFINITIONS ---
#define TRIGGER_PIN  13  // D7
#define ECHO_PIN     12  // D6
#define SERVO_PIN    14  // D5
#define MAX_DISTANCE 150

Adafruit_SSD1306 display(128, 64, &Wire, -1);
NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);
Servo   myServo;

// --- TIMING CONSTANTS ---
const int           MQTT_PORT         = 1883;
const int           threshold         = 60;
const int           departBuffer      = 30;
const unsigned long holdTime          = 45000;
const unsigned long departTime        = 6500;
const unsigned long cooldownTime      = 60000;
const unsigned long lidOpenDuration   = 5000;
const unsigned long resetSettleTime   = 2000;
const unsigned long displayInterval   = 200;
const unsigned long mqttRetryDelay    = 5000;
const unsigned long publishRetryDelay = 500;

// --- STATE VARIABLES ---
unsigned long detectionStartTime = 0;
unsigned long departStartTime    = 0;
unsigned long stateEntryTime     = 0;
unsigned long lastTriggerTime    = 0;
unsigned long lastDisplayUpdate  = 0;
unsigned long lastMqttAttempt    = 0;
unsigned long lastPublishAttempt = 0;

bool hasTriggered      = false;
bool hasPublishedFlush = false;

// systemState: 0 = IDLE/DETECT, 1 = FLUSHING, 2 = RESETTING
int systemState = 0;

// ---------------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  SerialAndTelnet.begin(115200);
  delay(1000);
  Wire.begin(4, 5);  // SDA=D2, SCL=D1

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 init failed - halting");
    while (true) { delay(1000); }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Booting...");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n====================================");
    Serial.println("WiFi Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.println("====================================");

    ArduinoOTA.setHostname("bathroom-flusher");
    ArduinoOTA.begin();

    client.setServer(mqtt_server, MQTT_PORT);
    reconnect();
  }

  myServo.attach(SERVO_PIN, 500, 2400);
  myServo.write(0);

  // lastTriggerTime == 0 means "never triggered" -> cooldown is clear on boot
  lastTriggerTime = 0;
}

// ---------------------------------------------------------------------------
// LOOP
// ---------------------------------------------------------------------------
void loop() {
  yield();
  SerialAndTelnet.handle();

  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
    if (!client.connected()) {
      unsigned long now = millis();
      if (now - lastMqttAttempt > mqttRetryDelay) {
        lastMqttAttempt = now;
        reconnect();
      }
    } else {
      client.loop();
    }
  }

  unsigned long currentTime = millis();

  int distance = 999;
  if (systemState == 0) {
    distance = getStableDistance();
  }

  bool updateDisplayNow = (currentTime - lastDisplayUpdate >= displayInterval);
  if (updateDisplayNow) {
    lastDisplayUpdate = currentTime;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    Serial.print("HC-SR04: ");
    if (distance == 999) Serial.println("OUT OF RANGE");
    else { Serial.print(distance); Serial.println(" cm"); }
  }

  switch (systemState) {
    case 1:  handleState1(currentTime, updateDisplayNow); break;
    case 2:  handleState2(currentTime, updateDisplayNow); break;
    default: handleState0(currentTime, distance, updateDisplayNow); break;
  }

  if (updateDisplayNow) {
    display.display();
  }
}

// ---------------------------------------------------------------------------
// STATE 0: IDLE / DETECTION
// ---------------------------------------------------------------------------
void handleState0(unsigned long currentTime, int distance, bool updateDisplayNow) {

  bool cooldownClear = (lastTriggerTime == 0) || (currentTime - lastTriggerTime >= cooldownTime);

  if (!cooldownClear) {
    if (updateDisplayNow) {
      unsigned long rem = (cooldownTime - (currentTime - lastTriggerTime)) / 1000;
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("RECHARGING");
      display.setTextSize(3);
      display.setCursor(25, 25);
      display.print(rem);
      display.print("s");
    }
    detectionStartTime = 0;
    hasTriggered       = false;
    departStartTime    = 0;
    return;
  }

  // treat out-of-range which is 999, as "person present"
  if (distance <= threshold || distance == 999) {
    departStartTime = 0;
    if (detectionStartTime == 0) detectionStartTime = currentTime;
    if (currentTime - detectionStartTime >= holdTime) {
      hasTriggered = true;
    }

  } else if (hasTriggered && distance > (threshold + departBuffer)) {
    if (departStartTime == 0) departStartTime = currentTime;

    if (currentTime - departStartTime >= departTime) {
      Serial.println("--- Departure confirmed, flushing ---");
      myServo.write(180);
      stateEntryTime     = currentTime;
      systemState        = 1;
      hasTriggered       = false;
      detectionStartTime = 0;
      departStartTime    = 0;
      return;
    }

  } else if (hasTriggered) {
    departStartTime = 0;

  } else {
    detectionStartTime = 0;
  }

  if (!updateDisplayNow) return;

  if (hasTriggered) {
    display.setTextSize(3);
    display.setCursor(15, 5);
    display.print("DONE!");
    display.fillRect(0, 45, 128, 19, SSD1306_WHITE);

  } else if (detectionStartTime > 0) {
    unsigned long elapsed = currentTime - detectionStartTime;
    display.setTextSize(3);
    display.setCursor(15, 5);
    display.print("HOLD!");
    int barWidth = map(elapsed, 0, holdTime, 0, 128);
    display.fillRect(0, 45, constrain(barWidth, 0, 128), 19, SSD1306_WHITE);

  } else {
    display.setTextSize(2);
    display.setCursor(20, 5);
    if (distance == 999) display.print("--- cm");
    else { display.print(distance); display.print(" cm"); }
    display.setTextSize(2);
    display.setCursor(35, 35);
    display.print("READY");
  }
}

// ---------------------------------------------------------------------------
// STATE 1: FLUSHING
// ---------------------------------------------------------------------------
void handleState1(unsigned long currentTime, bool updateDisplayNow) {

  if (!hasPublishedFlush && client.connected()) {
    if (currentTime - lastPublishAttempt > publishRetryDelay) {
      lastPublishAttempt = currentTime;
      if (client.publish("bathroom/flusher/event", "flushed")) {
        hasPublishedFlush = true;
      }
    }
  }

  if (updateDisplayNow) {
    display.setTextSize(3);
    display.setCursor(10, 20);
    display.print("FLUSH!");
  }

  if (currentTime - stateEntryTime >= lidOpenDuration) {
    myServo.write(0);
    lastTriggerTime = currentTime;
    stateEntryTime  = currentTime;
    systemState     = 2;
  }
}

// ---------------------------------------------------------------------------
// STATE 2: RESETTING
// ---------------------------------------------------------------------------
void handleState2(unsigned long currentTime, bool updateDisplayNow) {

  if (updateDisplayNow) {
    display.setTextSize(2);
    display.setCursor(10, 25);
    display.print("RESETTING");
  }

  if (currentTime - stateEntryTime >= resetSettleTime) {
    systemState       = 0;
    hasPublishedFlush = false;
  }
}

// ---------------------------------------------------------------------------
// HELPERS
// ---------------------------------------------------------------------------
int getStableDistance() {
  int cm = sonar.convert_cm(sonar.ping_median(9));
  return (cm == 0 || cm > MAX_DISTANCE) ? 999 : cm;
}

void reconnect() {
  if (client.connect("BathroomFlusher", mqtt_user, mqtt_pass,
                     "bathroom/flusher/status", 1, true, "offline")) {
    client.publish("bathroom/flusher/status", "online", true);

    String discoveryTopic = "homeassistant/sensor/bathroom_flusher/config";
    String payload = "{\"name\":\"Flush Event\","
                     "\"stat_t\":\"bathroom/flusher/event\","
                     "\"unique_id\":\"flusher_001\","
                     "\"dev\":{\"ids\":[\"flusher_001\"],"
                     "\"name\":\"Bathroom Flusher\","
                     "\"mdl\":\"NodeMCU\","
                     "\"mf\":\"DIY\"}}";
    client.publish(discoveryTopic.c_str(), payload.c_str(), true);
  }
}
