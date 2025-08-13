#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <HTTPClient.h>

// Replace these with your credentials and endpoints
#define WIFI_SSID                     ""
#define WIFI_PASSWORD                 ""
#define FIREBASE_PROJECT_URL          "" 
#define API_KEY_FB                    ""
#define USER_EMAIL                    ""
#define USER_PASSWORD                 ""
#define Google_API_MAPS               "" 
#define Google_geolocation_enpoint    ""
#define database_url_endpoint         ""

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
String uid;

// Define pins and baud for Serial2 (for GPS)
#define RXD2 16
#define TXD2 17
#define GPS_BAUD 9600
HardwareSerial gpsSerial(2);

//----------------------------------------------
// WiFi functions
//----------------------------------------------
void initWiFi() {
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long startAttemptTime = millis();
  // Wait up to 10 seconds
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    Serial.print(".");
    delay(1000);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi!");
  }
}

// Build the JSON payload from scanned WiFi networks
String buildWiFiScanPayload() {
  int no_scanned_networks = WiFi.scanNetworks(false, true);
  if (no_scanned_networks <= 0) {
    Serial.println("No WiFi networks found!");
    return "{}";
  }
  String payload = "{\n  \"wifiAccessPoints\": [\n";
  for (int i = 0; i < no_scanned_networks; i++) {
    payload += "    {\n";
    payload += "      \"macAddress\": \"" + WiFi.BSSIDstr(i) + "\",\n";
    payload += "      \"signalStrength\": " + String(WiFi.RSSI(i)) + "\n";
    payload += "    }";
    if (i < no_scanned_networks - 1) {
      payload += ",";
    }
    payload += "\n";
  }
  payload += "  ]\n}";
  return payload;
}

// Get approximate location via Google's Geolocation API using WiFi scan data
bool getWiFiLocation(double &lat, double &lng) {
  String postBody = buildWiFiScanPayload();
  if (postBody.equals("{}")) {
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected before geolocation request, reconnecting...");
    initWiFi();
  }
  HTTPClient http;
  http.begin(Google_geolocation_enpoint);
  http.addHeader("Content-Type", "application/json");
  Serial.println("Sending WiFi scan data to Google...");
  int httpCode = http.POST(postBody);
  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("Google response:");
    Serial.println(response);
    int latIndex = response.indexOf("\"lat\":");
    int lngIndex = response.indexOf("\"lng\":");
    if (latIndex == -1 || lngIndex == -1) {
      Serial.println("Could not parse lat/lng from response!");
      http.end();
      return false;
    }
    String latStr = response.substring(latIndex + 6, response.indexOf(",", latIndex));
    String lngStr = response.substring(lngIndex + 6, response.indexOf("\n", lngIndex));
    lat = latStr.toFloat();
    lng = lngStr.toFloat();
    http.end();
    return true;
  } else {
    Serial.printf("HTTP error code: %d\n", httpCode);
    String errMsg = http.getString();
    Serial.println(errMsg);
    http.end();
    return false;
  }
}

// Upload location and a Google Maps link to Firebase RTDB
bool uploadLocationToFirebase(double lat, double lng) {
  String mapLink = "https://www.google.com/maps/search/?api=1&query=" +
                   String(lat, 6) + "," + String(lng, 6);
  String latPath = "/wifiLocation/lat";
  String lngPath = "/wifiLocation/lng";
  String linkPath = "/wifiLocation/mapLink";
  bool okLat  = Firebase.RTDB.setFloat(&fbdo, latPath.c_str(), (float)lat);
  bool okLng  = Firebase.RTDB.setFloat(&fbdo, lngPath.c_str(), (float)lng);
  bool okLink = Firebase.RTDB.setString(&fbdo, linkPath.c_str(), mapLink);
  if (okLat && okLng && okLink) {
    Serial.println("Location + link uploaded to Firebase!");
    Serial.println("Map link: " + mapLink);
    return true;
  } else {
    Serial.print("Failed to upload location: ");
    Serial.println(fbdo.errorReason());
    return false;
  }
}

//----------------------------------------------
// Setup
//----------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Connect to WiFi
  initWiFi();

  // Configure Firebase
  config.api_key = API_KEY_FB;
  config.database_url = database_url_endpoint;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);
  config.token_status_callback = tokenStatusCallback; // callback function for token status
  config.max_token_generation_retry = 5;
  Firebase.begin(&config, &auth);

  // Wait until authenticated and get the UID
  Serial.println("Getting User UID...");
  while (auth.token.uid == "") {
    Serial.print(".");
    delay(1000);
  }
  uid = auth.token.uid.c_str();
  Serial.println();
  Serial.print("User UID: ");
  Serial.println(uid);

  // Start Serial2 for GPS data
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);
  Serial.println("Serial 2 started at 9600 baud");
}

//----------------------------------------------
// Main loop: continuously check WiFi & Firebase, then upload location every 5 minutes.
//----------------------------------------------
unsigned long lastUploadTime = 0;

void loop() {
  // Reconnect WiFi if disconnected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, attempting to reconnect...");
    initWiFi();
  }
  
  // Refresh Firebase token if expired
  if (Firebase.isTokenExpired()) {
    Serial.println("Firebase token expired, refreshing...");
    Firebase.refreshToken(&config);
  }
  
  // Check if 5 minutes have passed
  if (millis() - lastUploadTime >= 300000) {  // 300,000 ms = 5 minutes
    double latitude = 0.0, longitude = 0.0;
    if (getWiFiLocation(latitude, longitude)) {
      Serial.printf("Approx. location: Lat=%.6f, Lng=%.6f\n", latitude, longitude);
      uploadLocationToFirebase(latitude, longitude);
    } else {
      Serial.println("Failed to obtain WiFi-based location.");
    }
    lastUploadTime = millis();
  }
  
  // Optional: Process any incoming GPS data from Serial2
  while (gpsSerial.available() > 0) {
    char gpsData = gpsSerial.read();
    Serial.print(gpsData);
  }
  
  delay(1000); // Main loop delay to reduce CPU load
}
