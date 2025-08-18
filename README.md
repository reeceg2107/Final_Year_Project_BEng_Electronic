# Wi-Fi Geolocation + Voice Command (ESP32 + Nano 33 BLE Sense)

This two-board proof-of-concept demonstrates my skills in embedded systems, networking, and edge machine learning.

## ESP32
- Scans nearby Wi-Fi access points  
- Estimates location via Google Geolocation API  
- Uploads latitude, longitude, and a Google Maps link to Firebase Realtime Database every 5 minutes  

## Arduino Nano 33 BLE Sense
- Runs an Edge Impulse keyword-spotting model (“wake_up”) from its onboard PDM mic  
- On detection above a threshold, pulses an alert pin  

## Key Features
- Wi-Fi-based positioning (GPS alternative)  
- Cloud integration with Firebase RTDB  
- Auto-reconnect for Wi-Fi and token refresh  
- On-device ML inference using Edge Impulse  
- Simple serial logging for debugging  

## High-Level Flow
**ESP32**  
1. Connect to Wi-Fi  
2. Scan networks → send to Google API → parse `{lat, lng}`  
3. Upload to Firebase RTDB with a Google Maps link  

**Nano 33 BLE Sense**  
1. Capture audio via PDM mic  
2. Run continuous inference  
3. If "wake_up" detected with >0.5 confidence → pulse alert pin  

## Hardware
- ESP32 dev board (any common variant)  
- Arduino Nano 33 BLE Sense  

## Notes
- All credentials and API keys are placeholders for security  
- This repo is intended as a **portfolio showcase** rather than a deployable system  
