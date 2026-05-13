#define BLYNK_TEMPLATE_ID "XXXXXXXXXXXX"
#define BLYNK_TEMPLATE_NAME "Smart irrigation"
#define BLYNK_AUTH_TOKEN "XXXXXXXXXXXXXXXXXXXXX"

#include <WiFi.h> 
#include <HTTPClient.h>  
#include <ArduinoJson.h> 
#include <time.h> 
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <BlynkSimpleEsp32.h>

#define SOIL_PIN 34
#define RELAY_PIN 23
// Global weather variables
float weatherTemp    = 0;
int weatherHumidity  = 0;
int rainNow          = 0;
int rainIn1hr        = 0;
int rainIn2hr        = 0;
bool weatherFetched  = false;
int soilValue        = 0; 
int soilPercent = 0;
bool manualMode = false;
unsigned long lastCheckTime = 0;         
unsigned long checkInterval = 0; 
unsigned long lastPumpCheck = 0;
int pumpCmd          = 0;  
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
String lastAction = "NONE";

const char* ssid = "WIFI NAME**";
const char* password = "WIFI PASSWORD**";

#define API_KEY "firbase api key here***"
#define DATABASE_URL "https:// firebase database url here***"
#define USER_EMAIL "MAIL"   
#define USER_PASSWORD "PASSWORD" 

int getCurrentHour() {
  struct tm timeinfo;
  
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get time");
    return 10;  // fallback to 10am if fails
  }
  
  Serial.print("Current hour: ");
  Serial.println(timeinfo.tm_hour);
  
  return timeinfo.tm_hour;
}
String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "unknown";
  
  char buffer[30];
  sprintf(buffer, "%04d-%02d-%02d_%02d:%02d",
   timeinfo.tm_year + 1900,  
  timeinfo.tm_mon + 1,      
  timeinfo.tm_mday,         
  timeinfo.tm_hour,         
  timeinfo.tm_min         
  );
  
  return String(buffer);
}
void sendToBlynk() {
  Blynk.virtualWrite(V0, soilPercent);
  Blynk.virtualWrite(V1, weatherTemp);
  Blynk.virtualWrite(V2, weatherHumidity);
  Blynk.virtualWrite(V3, rainNow);
}
void sendToFirebase(int soil, int rain, float temp, int humidity, String action) {
  
  String timestamp = getTimestamp();
  String path = "/logs/" + timestamp;  // hint: use timestamp variable
  
  Firebase.RTDB.setInt(&fbdo,   path + "/soil",        soil);
  Firebase.RTDB.setInt(&fbdo,   path + "/rain",        rain);
  Firebase.RTDB.setFloat(&fbdo, path + "/temperature", temp);
  Firebase.RTDB.setInt(&fbdo,   path + "/humidity",    humidity);
  Firebase.RTDB.setString(&fbdo,path + "/action",      action);

  Serial.print("✅ Logged at: ");
  Serial.println(timestamp);
}

void fetchWeather() {
  HTTPClient http;
  http.begin("http://api.open-meteo.com/v1/forecast?latitude=18.454581&longitude=73.853517&hourly=temperature_2m,relative_humidity_2m,precipitation_probability&forecast_days=1&timezone=Asia%2FKolkata");
  
  int code = http.GET();
  Serial.print("Response code: ");
  Serial.println(code);
  
  if (code == 200) {
    String response = http.getString();  // hint: http.getString()
    Serial.println(response);
    int currentHour = getCurrentHour();
  // after getting response string, add:

    StaticJsonDocument<2048> doc;
    deserializeJson(doc, response);

    weatherTemp = doc["hourly"]["temperature_2m"][currentHour];
    weatherHumidity = doc["hourly"]["relative_humidity_2m"][currentHour]; 
    rainNow       = doc["hourly"]["precipitation_probability"][currentHour]; 
    rainIn1hr     = doc["hourly"]["precipitation_probability"][currentHour+1];
    rainIn2hr     = doc["hourly"]["precipitation_probability"][currentHour+2];
    weatherFetched   = true;

    Serial.print("Temp: ");     Serial.println(weatherTemp);
    Serial.print("Humidity: "); Serial.println(weatherHumidity);
    Serial.print("Rain now: "); Serial.println(rainNow);
    Serial.print("Rain +1hr: ");Serial.println(rainIn1hr);
    Serial.print("Rain +2hr: ");Serial.println(rainIn2hr);
      
      http.end(); 
  }
}
void makeWateringDecision(int soilValue, int rainNow, int rainIn1hr, int rainIn2hr) {

  // Scenario 3 first — highest priority
  if (rainNow > 85 || rainIn1hr > 85) {
    Serial.println("⛈ Rain expected — skipping watering");
    lastAction = "SKIPPED_RAIN.EXPECTED";
    return;
  }

  // Scenario 1 — soil dry, no rain
  if (soilValue > 2450 && rainNow < 20 && (rainIn1hr < 30 && rainIn2hr < 38)) {
    Serial.println("🌱 Dry soil, no rain → Full watering");
    digitalWrite(RELAY_PIN, LOW);
    delay(5000);
    digitalWrite(RELAY_PIN, HIGH);
    lastAction = "WATERED_FULL";
    return;
  }

  // Scenario 2 — soil medium, some rain expected
  if (soilValue > 1900 && (rainIn1hr >= 65 || rainIn2hr > 70 )) {
    Serial.println("🌱 Medium soil, rain likely → Partial watering");
    digitalWrite(RELAY_PIN, LOW);
    delay(4000);  // shorter duration
    digitalWrite(RELAY_PIN, HIGH);
    lastAction = "WATERED_PARTIAL";
    return;
  }

  Serial.println("💧 No watering needed");
  lastAction = "NO_ACTION";
}

void soilOnlyDecision(int soil) {
  Serial.println("⚠️ No WiFi — using soil only");
  
  if (soil > 2450) {
    Serial.println("🌱 Soil Dry → Pump ON");
    digitalWrite(RELAY_PIN, LOW);
    delay(5000);
    digitalWrite(RELAY_PIN, HIGH);
    Serial.println("⏹ Pump OFF");
    lastAction = "WATERED_FULL_NOWIFI";
  }
  else if (soil > 1900) {
    Serial.println("🌱 30% moisture → Pump ON");
    digitalWrite(RELAY_PIN, LOW);
    delay(4000);
    digitalWrite(RELAY_PIN, HIGH);
    Serial.println("⏹ Pump OFF");
    lastAction = "WATERED_PARTIALLY_NOWIFI";
  }
  else if (soil > 1550) {
    Serial.println("🌱 60% moisture → Pump ON");
    digitalWrite(RELAY_PIN, LOW);
    delay(3000);
    digitalWrite(RELAY_PIN, HIGH);
    Serial.println("⏹ Pump OFF");
    lastAction = "WATERED_PARTIALLY_NOWIFI";
  }
  else {
    Serial.println("💧 Soil Moist → No watering needed");
    lastAction = "NOT_WATERED_NOWIFI";
  }
}

BLYNK_WRITE(V5) {
  int val = param.asInt();
  if (val == 1) {
    Serial.println("📱 Manual pump ON");
    manualMode = true;
    digitalWrite(RELAY_PIN, LOW);
    Blynk.virtualWrite(V4, 1);
    Firebase.RTDB.setInt(&fbdo, "/pump", 1);  // ← sync to Firebase
  } else {
    Serial.println("📱 Manual pump OFF");
    manualMode = false;
    digitalWrite(RELAY_PIN, HIGH);
    Blynk.virtualWrite(V4, 0);
    Firebase.RTDB.setInt(&fbdo, "/pump", 0);  // ← sync to Firebase
  }
}
BLYNK_WRITE(V6) {
  int val = param.asInt();
  if (val == 1) {
    Serial.println("📱 Force check triggered from app");
    lastCheckTime = 0;      // reset timer
    checkInterval = 0;      // trigger immediately next loop
  }
}
  int soilToPercent(int raw) {
  int dry = 2800;  // adjust to your actual dry reading
  int wet = 1200;  // adjust to your actual wet reading
  int percent = map(raw, dry, wet, 0, 100);
  percent = constrain(percent, 0, 100);  // clamp between 0-100
  return percent;
}
void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Pump OFF

  delay(2000);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);

  if (Blynk.connected()) {
    Serial.println("✅ Blynk connected");
  } else {
    Serial.println("❌ Blynk not connected");
  }
  configTime(19800, 0, "pool.ntp.org");  // IST = UTC + 19800 seconds
  delay(2000); 
   int retry = 0;
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo) && retry < 20) {
  delay(500);
  retry++;
  Serial.print(".");
}
  Serial.println("\nTime synced");
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;          
  auth.user.password = USER_PASSWORD; 
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
   Serial.println("Firebase initialized");
}

void loop() {
  Blynk.run();

  // Check pump command every 3 seconds — fast enough for web control
  if (millis() - lastPumpCheck >= 3000) {
    lastPumpCheck = millis();
    
    if (Firebase.ready()) {
      if (Firebase.RTDB.getInt(&fbdo, "/pump", &pumpCmd)) {
        if (pumpCmd == 1 && !manualMode) {
          manualMode = true;
          digitalWrite(RELAY_PIN, LOW);
          Blynk.virtualWrite(V4, 1);
          Blynk.virtualWrite(V5, 1);
          Serial.println("🌐 Web: pump ON");
        } else if (pumpCmd == 0 && manualMode) {
          manualMode = false;
          digitalWrite(RELAY_PIN, HIGH);
          Blynk.virtualWrite(V4, 0);
          Blynk.virtualWrite(V5, 0);
          Serial.println("🌐 Web: pump OFF");
        }
      }
    }
  }

  // Main 30 min timer
  if (millis() - lastCheckTime >= checkInterval) {
    lastCheckTime = millis();

    soilValue = analogRead(SOIL_PIN);
    soilPercent = soilToPercent(soilValue);
    Serial.print("Soil: ");
    Serial.print(soilPercent);
    Serial.println("%");

    if (soilValue > 2450) {
      Blynk.logEvent("soil_dry", "🌱 Soil is dry! Watering initiated.");
    }

    fetchWeather();
    sendToBlynk();
    // Update current state in Firebase — web dashboard reads this
      if (Firebase.ready()) {
    Firebase.RTDB.setInt(&fbdo,   "/sensor/soil",        soilValue);
    Firebase.RTDB.setFloat(&fbdo, "/sensor/temperature", weatherTemp);
    Firebase.RTDB.setInt(&fbdo,   "/sensor/humidity",    weatherHumidity);
    Firebase.RTDB.setInt(&fbdo,   "/sensor/rain",        rainNow);
}

    if (!manualMode) {
      if (weatherFetched) {
        makeWateringDecision(soilValue, rainNow, rainIn1hr, rainIn2hr);
      } else {
        soilOnlyDecision(soilValue);
      }
    } else {
      Serial.println("📱 Manual mode active — skipping auto decision");
    }

    sendToFirebase(soilValue, rainNow, weatherTemp, weatherHumidity, lastAction);
    Serial.println("----------------------");

    if (lastAction == "WATERED_FULL" || lastAction == "WATERED_PARTIAL") {
      Serial.println("💤 Watered — next check in 2 hours");
      checkInterval = 2 * 60 * 60 * 1000UL;
    } else {
      Serial.println("💤 Checking again in 30 minutes");
      checkInterval = 30 * 60 * 1000UL;
    }
  }
}
