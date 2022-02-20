
// Include Libraries
#define BLYNK_TEMPLATE_ID BLYNK_TEMPLATE_ID_VALUE
#define BLYNK_DEVICE_NAME BLYNK_DEVICE_NAME_VALUE
#define BLYNK_AUTH_TOKEN BLYNK_AUTH_TOKEN_VALUE

#include "Arduino.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>

#include "Passwords.h"
#include "ESP32AnalogRead.h"
#include "Relay.h"
#include "InnerLoopTimer.h"
#include "MedianFiltering.h"

#define BLYNK_PRINT Serial

// Pin Definitions
#define RELAYMODULE_PIN_SIGNAL 25

// Global variables and defines

// Relay specific code.
Relay relayModule(RELAYMODULE_PIN_SIGNAL);

// Vars for ADC and Wifi
char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = SSID_VALUE;
char pass[] = WIFI_PASSWORD_VALUE;

const int voltagePin = 39; // P39 = ADC3
ESP32AnalogRead adc;
BlynkTimer blTimer; // Creating a timer object
InnerLoopTimer ilTimer;

// Moving average for reading ADC
#define WINDOW_SIZE 12            // 3.6 seconds
#define VLIMIT_COUNT_THRESHOLD 60 // 1 minute

MedianFilter<float> medianFilter(12);
const float vLimit = 3.4;
int curMaIndex = 0;
float Vadc = 0;
float cumVout = 0;
float VadcReadings[WINDOW_SIZE];
int vLimitCount = 0;
float vOutAvg = 0;
float vOutMedian = 0.0f;

void blynkTimer()
{
    Serial.println("Voltage = " + String(Vadc, 3) + " V");
    Serial.println("Voltage Avg = " + String(vOutAvg, 3) + " V");
    Serial.println("Voltage Median = " + String(vOutMedian, 3) + " V");

    Serial.println("Sending to Blynk ...");

    Blynk.virtualWrite(V5, vOutAvg);
    Blynk.virtualWrite(V6, vOutMedian);
    Blynk.virtualWrite(V7, Vadc);

    if (vOutAvg < vLimit)
    {
        if (vLimitCount)
        {
            Serial.println("Voltage back within acceptable values");
        }
        vLimitCount = 0;
        return;
    }

    vLimitCount = vLimitCount + 1;
    Serial.println("Warning, voltage higher than threshold = " + String(vLimit, 3) + "V");
    Serial.println("Shutting down in " + String((VLIMIT_COUNT_THRESHOLD - vLimitCount) * 5) + " seconds unless regular voltage levels are restored.");

    // Blynk.logEvent("high_voltage", "Warning, voltage higher than threshold = " + String(vLimit, 3) +
    //          "V. Shutting down in " + String((VLIMIT_COUNT_THRESHOLD - vLimitCount) * 5) + " seconds unless regular voltage levels are restored.");

    if (vLimitCount == VLIMIT_COUNT_THRESHOLD)
    {

        Serial.println("Shutting down power supply");
        Blynk.logEvent("psu_shutdown", "shutting down power supply");
        relayModule.on();
        vLimitCount = 0;
    }
}

void innerLoopTimer()
{
    // Please don't send more that 10 values per second.
    Vadc = adc.readVoltage() * 2;
    vOutMedian = medianFilter.AddValue(Vadc);

    cumVout = cumVout - VadcReadings[curMaIndex]; // Remove the oldest entry from the sum
    VadcReadings[curMaIndex] = vOutMedian;              // update the last index.
    cumVout = cumVout + vOutMedian;                     // Add the newest reading to the sum
    curMaIndex = (curMaIndex + 1) % WINDOW_SIZE;  // Increment the index, and wrap to 0 if it exceeds the window size

    vOutAvg = cumVout / WINDOW_SIZE; // Divide the sum of the window by the window size for the result
}

// Setup the essentials for your circuit to work. It runs first every time your circuit is powered with electricity.
void setup()
{
    // Setup Serial which is useful for debugging
    // Use the Serial Monitor to view printed messages
    Serial.begin(9600);
 
    // Detecting chip type... ESP32
    // espefuse.py v3.3-dev
    // ADC VRef calibration: 1093mV
    adc.attach(voltagePin);
    adc.checkEfuse();
    vLimitCount = 0;

    Serial.println("Connecting to " + String(ssid) + "...");
    blTimer.setInterval(1000L, blynkTimer); // Starting a Blynk timer
    ilTimer.setInterval(400L, innerLoopTimer);

    // Wifi specific statements.
    Blynk.connectWiFi(ssid, pass);
    Serial.println("Connected to " + String(ssid));
    Serial.println("DNS IP: " + WiFi.dnsIP().toString());
    Serial.println("IP: " + WiFi.localIP().toString());
    Blynk.config(auth);
}

BLYNK_CONNECTED()
{
    Blynk.syncVirtual(V1);
}

BLYNK_WRITE(V1) // Executes when the value of virtual pin 0 changes
{
    if (param.asInt() == 1 && vOutAvg < vLimit)
    {
        // execute this code if the switch widget is now ON
        Serial.println("Turning on the power supply");
        relayModule.on();
    }
    else
    {
        // execute this code if the switch widget is now OFF
        Serial.println("Turning off the power supply");
        relayModule.off();
    }
}

// Main logic of your circuit. It defines the interaction between the components you selected. After setup, it runs over and over again, in an eternal loop.
void loop()
{
    Blynk.run();
    ilTimer.run();
    blTimer.run();
}