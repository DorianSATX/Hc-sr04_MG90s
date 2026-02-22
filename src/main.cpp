#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Servo.h>
#include <NewPing.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include "secrets.h" 

// --- WIFI CREDENTIALS ---
const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;

// --- PIN DEFINITIONS ---
#define TRIGGER_PIN  13 // D7
#define ECHO_PIN     12 // D6
#define SERVO_PIN    14 // D5 
#define MAX_DISTANCE 150 

Adafruit_SSD1306 display(128, 64, &Wire, -1);
NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);
Servo myServo;

// --- SETTINGS ---
const int threshold = 55; 
const int buffer = 10;
const unsigned long holdTime = 2000;          
const unsigned long cooldownTime = 60000;
const unsigned long lidOpenDuration = 5000; 

// --- STATE VARIABLES ---
unsigned long detectionStartTime = 0;
unsigned long lastTriggerTime = 0;
unsigned long servoActionTimer = 0;
bool hasTriggered = false;
int systemState = 0; 

int getStableDistance() {
  int cm = sonar.convert_cm(sonar.ping_median(5)); 
  return (cm == 0 || cm > MAX_DISTANCE) ? 999 : cm;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(4, 5); 

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;); 
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.setHostname("bathroom-flusher");
    ArduinoOTA.begin();

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("WiFi Connected");
    display.print("IP: "); 
    display.println(WiFi.localIP()); 
    display.display();
    delay(2000); 
  }

  myServo.attach(SERVO_PIN, 500, 2400); 
  myServo.write(0);
  lastTriggerTime = millis() - cooldownTime;
}
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle(); 
  }

  unsigned long currentTime = millis();
  int distance = 999;
  
  if (systemState == 0) {
      distance = getStableDistance();
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // --- STATE 1: FLUSHING ---
  if (systemState == 1) {
    display.setTextSize(3);
    display.setCursor(10, 20);
    display.print("FLUSH!");
    if (currentTime - servoActionTimer >= lidOpenDuration) {
      myServo.write(0);
      servoActionTimer = currentTime;
      systemState = 2; 
    }
  } 
  // --- STATE 2: RESETTING ---
  else if (systemState == 2) {
    display.setTextSize(2);
    display.setCursor(10, 25);
    display.print("RESETTING");
    if (currentTime - servoActionTimer >= 2000) { 
      systemState = 0;
      lastTriggerTime = millis(); 
    }
  } 
  // --- STATE 0: IDLE / DETECTION ---
  else {
    if (currentTime - lastTriggerTime >= cooldownTime) {
      // IF WE ARE IN THE MIDDLE OF A COUNTDOWN OR READY TO FLUSH
      if (distance <= threshold || hasTriggered) {
        
        if (detectionStartTime == 0) detectionStartTime = currentTime;
        unsigned long elapsed = currentTime - detectionStartTime;
        
        if (elapsed >= holdTime) {
          hasTriggered = true;
        }

        if (hasTriggered) {
          // --- STICKY LOGIC: WAITING FOR YOU TO WALK AWAY ---
          display.setTextSize(3);
          display.setCursor(15, 5);
          display.print("DONE!"); 
          display.fillRect(0, 45, 128, 19, SSD1306_WHITE); // Bar stays full

          // Trigger ONLY when you move away (Distance > threshold + buffer)
          // We allow 999 here because if you leave, we WANT it to flush!
          if (distance > (threshold + buffer)) {
            myServo.write(180);
            servoActionTimer = currentTime;
            systemState = 1;
            hasTriggered = false;
            detectionStartTime = 0;
          }
        } else {
          // --- STILL COUNTING DOWN ---
          display.setTextSize(3); 
          display.setCursor(15, 5);
          display.print("HOLD!"); 
          int barWidth = map(elapsed, 0, holdTime, 0, 128);
          display.fillRect(0, 45, constrain(barWidth, 0, 128), 19, SSD1306_WHITE);
        }
      } 
      else {
        // --- IDLE / READY MODE ---
        display.setTextSize(2);
        display.setCursor(20, 5);
        display.print(distance); display.print(" cm"); 
        
        display.setTextSize(2);
        display.setCursor(35, 35);
        display.print("READY");
        
        detectionStartTime = 0; 
      }
    } 
    else {
      // --- COOLDOWN MODE ---
      unsigned long rem = (cooldownTime - (currentTime - lastTriggerTime)) / 1000;
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("RECHARGING");

      display.setTextSize(3); 
      display.setCursor(25, 25);
      display.print(rem); display.print("s");
    }
  }
  display.display();
}