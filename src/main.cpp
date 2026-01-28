#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Servo.h>
#include <NewPing.h>

// --- PIN DEFINITIONS (NodeMCU) ---
#define TRIGGER_PIN  13 // D7
#define ECHO_PIN     12 // D6
#define SERVO_PIN    14 // D5
#define MAX_DISTANCE 150 // Increased to 150cm to reduce "---" readings

// --- OBJECTS ---
Adafruit_SSD1306 display(128, 64, &Wire, -1);
NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);
Servo myServo;

// --- SETTINGS ---
const int threshold = 55; 
const int buffer = 10;
const unsigned long holdTime = 5000;
const unsigned long cooldownTime = 60000;
const unsigned long lidOpenDuration = 2000; 

// --- STATE VARIABLES ---
unsigned long detectionStartTime = 0;
unsigned long lastTriggerTime = 0;
unsigned long servoActionTimer = 0;
bool hasTriggered = false;
int systemState = 0; 

// --- HELPERS ---
int getStableDistance() {
  // Use 15 pings (slightly faster than 20) for a good balance of speed and accuracy
  int cm = sonar.convert_cm(sonar.ping_median(15));
  if (cm == 0 || cm > MAX_DISTANCE) {
    return 999; 
  }
  return cm;
}

void drawProgressBar(int percentage) {
  int barWidth = 100;
  int x = (128 - barWidth) / 2;
  display.drawRect(x, 54, barWidth, 8, SSD1306_WHITE);
  int progress = map(percentage, 0, 100, 0, barWidth - 4);
  display.fillRect(x + 2, 56, progress, 4, SSD1306_WHITE);
}

// --- CORE FUNCTIONS ---
void setup() {
  Wire.begin(4, 5); // SDA=D2, SCL=D1

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;); 
  }

  // Set cooldown as finished on startup
  lastTriggerTime = millis() - cooldownTime;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(35, 25);
  display.print("ARMED!");
  display.display();
  delay(1000);
  
  myServo.attach(SERVO_PIN, 500, 2500); 
  myServo.write(0); 
}

void loop() {
  int rawDistance = getStableDistance();
  int distance;

  // --- GLITCH FILTER ---
  // If we get '---', check one more time before accepting it as empty
  if (rawDistance == 999) {
    delay(20); 
    int secondCheck = getStableDistance();
    distance = (secondCheck != 999) ? secondCheck : 999;
  } else {
    distance = rawDistance;
  }

  unsigned long currentTime = millis();
  display.clearDisplay();

  // 1. STATE: LID IS OPEN 
  if (systemState == 1) {
    display.setTextSize(3);
    display.setCursor(20, 20);
    display.print("OPEN!");
    if (currentTime - servoActionTimer >= lidOpenDuration) {
      myServo.write(0);
      servoActionTimer = currentTime;
      systemState = 2; 
    }
  } 
  
  // 2. STATE: LID IS CLOSING
  else if (systemState == 2) {
    display.setTextSize(2);
    display.setCursor(20, 20);
    display.print("CLOSING");
    if (currentTime - servoActionTimer >= 2000) { 
      systemState = 0;
      lastTriggerTime = millis(); 
    }
  } 
  
  // 3. STATE: MONITORING & COOLDOWN
  else {
    // LARGE DISTANCE DISPLAY (Size 3 number, Size 2 label)
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.print("D:"); 
    display.setCursor(35, 0); 
    display.setTextSize(3); 
    if (distance == 999) {
      display.print("---");
    } else {
      display.print(distance);
    }
    display.setTextSize(1);
    display.print(" cm");

    // COOLDOWN LOGIC
    if (currentTime - lastTriggerTime < cooldownTime) {
      unsigned long remaining = cooldownTime - (currentTime - lastTriggerTime);
      display.setTextSize(2);
      display.setCursor(10, 30);
      display.print("WAIT "); display.print(remaining / 1000); display.print("s");
      drawProgressBar(map(remaining, 0, cooldownTime, 0, 100));
    } 
    
    // DETECTION LOGIC
    else if (distance <= threshold) {
      if (detectionStartTime == 0) detectionStartTime = currentTime;
      unsigned long elapsed = currentTime - detectionStartTime;
      
      display.setTextSize(2);
      if (elapsed >= holdTime) {
        hasTriggered = true;
        display.setCursor(25, 30); 
        display.print("ARMED!");
        drawProgressBar(100);
      } else {
        display.setCursor(15, 30);
        display.print("HOLD "); display.print((holdTime - elapsed) / 1000); display.print("s");
        drawProgressBar(map(elapsed, 0, holdTime, 0, 100));
      }
    } 
    
   // 4. Logic: Trigger (Only if person moves away, NOT if sensor fails)
    else if (hasTriggered && distance > (threshold + buffer) && distance != 999) {
      myServo.write(180);
      servoActionTimer = currentTime;
      systemState = 1;
      hasTriggered = false;
    }
    
    // IDLE SCANNING
    else {
      detectionStartTime = 0;
      display.setCursor(25, 45);
      display.setTextSize(1);
      display.print("Scanning...");
    }
  }

  display.display();
  delay(30); 
}