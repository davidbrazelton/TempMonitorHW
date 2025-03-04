#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Pin Assignments
#define TEMP_SENSOR_PIN 20 // DS18B20 data pin
#define BUZZER_PIN 6       // Passive Buzzer
#define LED_PIN 10         // Alarm Indicator LED

// OneWire and Temperature Sensor
OneWire oneWire(TEMP_SENSOR_PIN);
DallasTemperature sensors(&oneWire);

// Mode Selection
enum Mode
{
  FREEZER,
  FRIDGE,
  TEST
};
Mode currentMode;

// Temperature Thresholds
const float freezerThreshold = -18.0;
const float fridgeThreshold = 4.0;
float highTempThreshold;
int alarmDelayMinutes;

const float recoveryThreshold = 2.0;
unsigned long highTempStartTime = 0;
float currentTemperature = 0.0; // ✅ Holds the latest temperature reading
bool warningActive = false; // Tracks if we're in the warning state
bool alarmActive = false;   // ✅ Tracks if the alarm is currently active

void logMessage(const char *level, const char *message)
{
  Serial.print("[");
  Serial.print(level);
  Serial.print("] ");
  Serial.println(message);
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  logMessage("INFO", "🚀 System Starting...");
  logMessage("INFO", "Initializing DS18B20 Sensor...");

  sensors.begin();
  logMessage("INFO", "✅ DS18B20 Initialized!");

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // ✅ Manually Initialize LEDC for Buzzer (ESP32 fix)
  ledcSetup(0, 2000, 8);        // Channel 0, 2kHz frequency, 8-bit resolution
  ledcAttachPin(BUZZER_PIN, 0); // Attach buzzer to LEDC channel 0

  logMessage("INFO", "✅ LEDC Initialized for Buzzer.");

  // Take the first two temperature readings
  float temp1, temp2, avgTemp;
  do
  {
    sensors.requestTemperatures();
    temp1 = sensors.getTempCByIndex(0);
    delay(1000);
    sensors.requestTemperatures();
    temp2 = sensors.getTempCByIndex(0);
  } while (temp1 == -127.0 || temp2 == -127.0); // Retry if sensor wasn't ready

  avgTemp = (temp1 + temp2) / 2.0;

  Serial.print("[BOOT] First Temperature Readings: ");
  Serial.print(temp1);
  Serial.print("°C, ");
  Serial.print(temp2);
  Serial.println("°C");

  Serial.print("[BOOT] Average Temperature: ");
  Serial.print(avgTemp);
  Serial.println("°C");

  // Determine mode based on initial readings
  if (avgTemp < freezerThreshold)
  {
    currentMode = FREEZER;
    highTempThreshold = freezerThreshold;
    alarmDelayMinutes = 20;
    logMessage("MODE", "🧊 Freezer Mode - Danger Temp: -18°C, Alarm in 20 min");
  }
  else if (avgTemp >= freezerThreshold && avgTemp < fridgeThreshold)
  {
    currentMode = FRIDGE;
    highTempThreshold = fridgeThreshold;
    alarmDelayMinutes = 20;
    logMessage("MODE", "🥶 Refrigerator Mode - Danger Temp: 4°C, Alarm in 20 min");
  }
  else
  {
    currentMode = TEST;
    highTempThreshold = avgTemp + 2.0; // ✅ Set threshold 2°C below average temp
    alarmDelayMinutes = 1; // ✅ TEST mode now has 1-minute alarm delay
    logMessage("MODE", ("🛠️ Test Mode - Dynamic Danger Temp: " + String(highTempThreshold) + "°C").c_str());
  }
}

float getTemperature()
{
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}

void triggerWarning()
{
  warningActive = true;
  highTempStartTime = millis(); // Start warning timer
  logMessage("TRIGGER", "⚠️ WARNING: Temperature is high. Monitoring...");
}

void triggerAlarm()
{
  warningActive = false; // ✅ Exit warning state in favor of Alarm state
  alarmActive = true;    // ✅ Track alarm state
  logMessage("TRIGGER", "🚨 ALARM: Temperature too high!");

  if (!ledcRead(0))
  { // ✅ Check if LEDC is active before writing tone
    logMessage("WARN", "⚠️ LEDC was not initialized, setting up again...");
    ledcSetup(0, 2000, 8);
    ledcAttachPin(BUZZER_PIN, 0);
  }
  digitalWrite(LED_PIN, HIGH);
  ledcWriteTone(0, 2000); // ✅ Use LEDC tone function instead of tone()
}

void doCheckTemperature()
{
  currentTemperature = getTemperature();
  Serial.print("[TEMP] Current Temp: ");
  Serial.print(currentTemperature);
  Serial.println(" °C");

  if (currentTemperature == -127.0)
  {
    logMessage("ERROR", "❌ Sensor Not Detected! Check wiring.");
    delay(5000);
  }
}

void doWarningCheck()
{
  if(alarmActive) return; // ✅ Skip warning check if alarm is active

  if (warningActive)
  {
    logMessage("WARNING", "⚠️ WARNING: Temperature is high. Monitoring...");

    unsigned long timePassed = (millis() - highTempStartTime) / 60000;
    unsigned long timeLeft = alarmDelayMinutes - timePassed;

    if (timeLeft > 0)
    {
      Serial.print("[WARNING] Time until alarm: ");
      Serial.print(timeLeft);
      Serial.println(" minutes");
    }

    if (timePassed >= alarmDelayMinutes)
    {
      triggerAlarm();
    }
  }
  else if (currentTemperature > highTempThreshold && !warningActive)
  {
    triggerWarning();
  }
}

void doAlarmCheck()
{
  if (warningActive && alarmActive)
  {
    warningActive = false; // ✅ Exit warning state in favor of Alarm state
  } else if(alarmActive) {
    logMessage("ALARM", "🚨 ALARM: Temperature too high!");
  }
}

void doRecoveryCheck()
{
  if (currentTemperature <= highTempThreshold && (warningActive || alarmActive))
  {
    logMessage("INFO", "✅ Temperature recovered. Exiting warning/alarm state.");
    warningActive = false;
    alarmActive = false;
    highTempStartTime = 0;
    digitalWrite(LED_PIN, LOW);
    ledcWriteTone(0, 0); // ✅ Stop tone using LEDC function
  }
}

void loop()
{
  doCheckTemperature();
  doWarningCheck();
  doAlarmCheck();
  doRecoveryCheck();
  delay(5000);
}
