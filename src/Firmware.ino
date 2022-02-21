/*
 * Copyright (c) 2022 Alessandro Curzi
 * 
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and limitations under the License
 */

#include "Passwords.h"

#define BLYNK_TEMPLATE_ID BLYNK_TEMPLATE_ID_VALUE
#define BLYNK_DEVICE_NAME BLYNK_DEVICE_NAME_VALUE
#define BLYNK_AUTH_TOKEN BLYNK_AUTH_TOKEN_VALUE

#include "Arduino.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>

#include "ESP32AnalogRead.h"
#include "Relay.h"
#include "InnerLoopTimer.h"
#include "MedianFiltering.h"

#define BLYNK_PRINT Serial

// Pin Definitions
#define RELAYMODULE_PIN_SIGNAL 25
#define ADC_PIN 39                                   // P39 = ADC3

// Moving average for reading ADC
#define WINDOW_SIZE 12                               // 3.6 seconds
#define VLIMIT_COUNT_THRESHOLD 60                    // 1 minute

// Objects and Timers
Relay relayModule(RELAYMODULE_PIN_SIGNAL);
ESP32AnalogRead adc;
BlynkTimer blTimer;
InnerLoopTimer ilTimer;

const int voltagePin = ADC_PIN; 

// Vars for Wifi
char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = SSID_VALUE;
char pass[] = WIFI_PASSWORD_VALUE;

// ADC median and moving average related variables
MedianFilter<float> medianFilter(12);
float vLimit = 3.4;
int curMaIndex = 0;
float Vadc = 0;
float cumVout = 0;
float VadcReadings[WINDOW_SIZE];
int vLimitCount = 0;
float vOutAvg = 0;
float vOutMedian = 0.0f;

// This timer controls WiFi communication with Blynk, and checks on the cut-off voltage levels.
void blynkTimer()
{
    Serial.println("Voltage = " + String(Vadc, 3) + " V");
    Serial.println("Voltage Avg = " + String(vOutAvg, 3) + " V");
    Serial.println("Voltage Median = " + String(vOutMedian, 3) + " V");

    Serial.println("Sending to Blynk ...");
    Blynk.virtualWrite(V5, vOutAvg);     // V5 is displayed on a Gauge object and on a super chart on Blynk
    Blynk.virtualWrite(V6, vOutMedian);  // V6 is displayed on a Gauge object and on a super chart on Blynk
    Blynk.virtualWrite(V7, Vadc);        // V7 is displayed on a Gauge object and on a super chart on Blynk

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

    // Had to comment the logEvent below due to quota issues on Blynk :)
    // Blynk.logEvent("high_voltage", "Warning, voltage higher than threshold = " + String(vLimit, 3) +
    //          "V. Shutting down in " + String((VLIMIT_COUNT_THRESHOLD - vLimitCount) * 5) + " seconds unless regular voltage levels are restored.");

    if (vLimitCount == VLIMIT_COUNT_THRESHOLD)
    {
        Serial.println("Shutting down power supply");
        Blynk.logEvent("psu_shutdown", "shutting down power supply");
        relayModule.on(); // Despite the on state, this turns off the PSU
        vLimitCount = 0;
        Blynk.virtualWrite(V1, 0);
    }
}

// This timer samples the voltage from the ADC.
void innerLoopTimer()
{
    // Please don't send more that 10 values per second.
    Vadc = adc.readVoltage() * 2;
    vOutMedian = medianFilter.AddValue(Vadc);

    cumVout = cumVout - VadcReadings[curMaIndex]; // Remove the oldest entry from the sum
    VadcReadings[curMaIndex] = vOutMedian;        // update the last index.
    cumVout = cumVout + vOutMedian;               // Add the newest reading to the sum
    curMaIndex = (curMaIndex + 1) % WINDOW_SIZE;  // Increment the index, and wrap to 0 if it exceeds the window size
    vOutAvg = cumVout / WINDOW_SIZE;              // Divide the sum of the window by the window size for the result
}

// Setup the essentials for your circuit to work. It runs first every time your circuit is powered up.
void setup()
{
    // Starting with the PSU in off mode
    relayModule.on(); // Despite the on state, this turns off the PSU

    // Setup Serial which is useful for debugging
    // Use the Serial Monitor to view printed messages
    Serial.begin(9600);
 
    // Setting up the ADC: Attaching and reading calibrated VRef value.
    // ADC VRef calibration: 1093mV for my current board.
    adc.attach(ADC_PIN);
    adc.checkEfuse();
    vLimitCount = 0;

    // Setting up timers for loop activities.
    blTimer.setInterval(1000L, blynkTimer);    // Starting a Blynk timer.
    ilTimer.setInterval(400L, innerLoopTimer); // Starting a higher frequency timer for ADC sampling.

    // Wifi Initialization.
    Serial.println("Connecting to " + String(ssid) + "...");
    Blynk.connectWiFi(ssid, pass);
    Serial.println("Connected to " + String(ssid));
    Serial.println("DNS IP: " + WiFi.dnsIP().toString());
    Serial.println("IP: " + WiFi.localIP().toString());
    Blynk.config(auth);
}

// Syncs UI objects.
BLYNK_CONNECTED()
{
    Blynk.syncVirtual(V1);
    Blynk.syncVirtual(V8);
}

// Executes when the value of virtual pin V1 changes
// V1 is connected to a button which controls the PSU ON/OFF state.
BLYNK_WRITE(V1)
{
    if (param.asInt() == 1 && vOutAvg < vLimit)
    {
        // execute this code if the switch widget is now ON
        Serial.println("Turning on the power supply");
        relayModule.off(); // Despite the off state, this turns on the PSU
    }
    else
    {
        // execute this code if the switch widget is now OFF
        Serial.println("Turning off the power supply");
        relayModule.on(); // Despite the on state, this turns off the PSU
    }
}

// Executes when the value of virtual pin V8 changes
// V8 is connected to a slider which controls the cutoff voltage.
// For the PSU I'm controlling, the range is 3.30V to 3.66V.
BLYNK_WRITE(V8)
{
    vLimit = param.asFloat();
}

// Main logic.
void loop()
{
    Blynk.run();
    ilTimer.run();
    blTimer.run();
}