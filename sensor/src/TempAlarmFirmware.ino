/*
 * Copyright (C) 2015-2021 Gregory S. Meiste  <http://gregmeiste.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <Adafruit_DHT.h>
#include <Adafruit_IO_Client.h>

#define SERIAL             Serial   // USB port
//#define SERIAL           Serial1  // TX/RX pins
#define SERIAL_BAUD        115200

#define DHT_PIN            D2
#define DHT_TYPE           DHT22

#define LED_PIN            D7

#define SENSOR_CHECK_MS    2000
#define AIO_CHECK_MS       60000
#define ALARM_MS           60000

#define SETTINGS_ADDR      0x0000
#define SETTINGS_MAGIC_NUM 0xdeb8ab1e
#define DEFAULT_REPORT_MS  120000
#define DEFAULT_LOW_THRES  40
#define DEFAULT_HIGH_THRES 80

#define MAX_READING_DELTA  5.0

// Adafruit IO API Key length
#define AIO_KEY_LEN        32

#define USE_ADAFRUIT_IO

struct SensorSettings
{
    uint32_t magic;
    unsigned int reportMillis;
    char aioKey[AIO_KEY_LEN + 1];
    int lowThres;
    int highThres;
};

SensorSettings sensorSettings;
int currentTempF = 0;
int currentHumid = 0;
bool haveValidReading = false;

DHT dht(DHT_PIN, DHT_TYPE);

#ifdef USE_ADAFRUIT_IO
TCPClient tcpClient;
Adafruit_IO_Client* AIOClient;
#endif

static void doAlarm(void)
{
    char publishString[32];

    SERIAL.println("Publishing alarm event");

    snprintf(publishString, sizeof(publishString), "{\"tempF\": %d}", currentTempF);
    Particle.publish("tempAlarm", publishString, PRIVATE);
}

static int setReportRate(String rate)
{
    sensorSettings.reportMillis = atoi(rate.c_str());
    EEPROM.put(SETTINGS_ADDR, sensorSettings);

    SERIAL.printlnf("New reportMillis: %u", sensorSettings.reportMillis);

    return 0;
}

static int setAIOKey(String aioKey)
{
    if (aioKey.length() == AIO_KEY_LEN)
    {
        aioKey.getBytes((unsigned char *)sensorSettings.aioKey, sizeof(sensorSettings.aioKey));
        EEPROM.put(SETTINGS_ADDR, sensorSettings);

        SERIAL.printlnf("New AIO Key: %s", sensorSettings.aioKey);

        return 0;
    }

    return -1;
}

static int setLowThres(String lowThres)
{
    sensorSettings.lowThres = atoi(lowThres.c_str());
    EEPROM.put(SETTINGS_ADDR, sensorSettings);

    SERIAL.printlnf("New Low Threshold: %d", sensorSettings.lowThres);

#ifdef USE_ADAFRUIT_IO
    // Keep Adafruit IO in sync
    Adafruit_IO_Feed lowThresFeed = AIOClient->getFeed("temp-alarm.low-threshold");
    char publishString[8];
    snprintf(publishString, sizeof(publishString), "%d", sensorSettings.lowThres);
    if (!lowThresFeed.send(publishString))
    {
        SERIAL.println("Failed to publish low threshold to Adafruit IO!");
    }
#endif

    return 0;
}

static int setHighThres(String highThres)
{
    sensorSettings.highThres = atoi(highThres.c_str());
    EEPROM.put(SETTINGS_ADDR, sensorSettings);

    SERIAL.printlnf("New High Threshold: %d", sensorSettings.highThres);

#ifdef USE_ADAFRUIT_IO
    // Keep Adafruit IO in sync
    Adafruit_IO_Feed highThresFeed = AIOClient->getFeed("temp-alarm.high-threshold");
    char publishString[8];
    snprintf(publishString, sizeof(publishString), "%d", sensorSettings.highThres);
    if (!highThresFeed.send(publishString))
    {
        SERIAL.println("Failed to publish high threshold to Adafruit IO!");
    }
#endif

    return 0;
}

static int alarmTest(String used)
{
    doAlarm();
    return 0;
}

static void doAlarmIfNecessary(void)
{
    static unsigned long lastAlarmMillis = 0;
    static bool sentAlarm = false;
    unsigned long now = millis();

#ifdef USE_ADAFRUIT_IO
    static unsigned long lastAIOThresMillis = 0;

    if ((now - lastAIOThresMillis) >= AIO_CHECK_MS)
    {
        SERIAL.println("Getting thresholds from Adafruit IO");

        // Get latest values from Adafruit IO
        Adafruit_IO_Feed lowThresFeed = AIOClient->getFeed("temp-alarm.low-threshold");
        FeedData lowThresLatest = lowThresFeed.receive();
        if (lowThresLatest.isValid())
        {
            int newLowThres;
            lowThresLatest.intValue(&newLowThres);
            if ((newLowThres != 0) && (newLowThres != sensorSettings.lowThres))
            {
                sensorSettings.lowThres = newLowThres;
                EEPROM.put(SETTINGS_ADDR, sensorSettings);

                SERIAL.printlnf("New Low Threshold: %d", sensorSettings.lowThres);
            }
        }
        else
        {
            SERIAL.println("Adafruit IO low threshold is not valid!");
        }

        // Yield to Particle before retrieving the high threshold
        Particle.process();

        Adafruit_IO_Feed highThresFeed = AIOClient->getFeed("temp-alarm.high-threshold");
        FeedData highThresLatest = highThresFeed.receive();
        if (highThresLatest.isValid())
        {
            int newHighThres;
            highThresLatest.intValue(&newHighThres);
            if ((newHighThres != 0) && (newHighThres != sensorSettings.highThres))
            {
                sensorSettings.highThres = newHighThres;
                EEPROM.put(SETTINGS_ADDR, sensorSettings);

                SERIAL.printlnf("New High Threshold: %d", sensorSettings.highThres);
            }
        }
        else
        {
            SERIAL.println("Adafruit IO high threshold is not valid!");
        }

        lastAIOThresMillis = now;
    }
#endif

    // Check if temperature is outside the allowed range
    if ((currentTempF < sensorSettings.lowThres) || (currentTempF > sensorSettings.highThres))
    {
        // Only alarm if enough time has passed from last alarm to avoid spamming user
        if (!sentAlarm && ((now - lastAlarmMillis) >= ALARM_MS))
        {
            doAlarm();
            lastAlarmMillis = now;
            sentAlarm = true;
        }
    }
    else
    {
        // All clear!
        sentAlarm = false;
    }

}

static void doMonitorIfTime(void)
{
    static unsigned long lastReadingMillis = 0;
    unsigned long now = millis();

    if ((now - lastReadingMillis) >= SENSOR_CHECK_MS)
    {
        float h = dht.getHumidity();
        float f = dht.getTempFarenheit();

        float delta_temp = f - currentTempF;

        // Check if reading failed
        if (isnan(h) || isnan(f))
        {
            SERIAL.println("Failed to read from DHT sensor!");
            digitalWrite(LED_PIN, HIGH);
        }
        // Sanity check temperature read from sensor. If delta from previous
        // reading is greater than MAX_READING_DELTA degrees, it probably is bogus.
        else if (haveValidReading && (fabsf(delta_temp) > MAX_READING_DELTA))
        {
            SERIAL.printlnf("Bad reading (%.2f) from DHT sensor!", f);
            digitalWrite(LED_PIN, HIGH);
        }
        // Reading is good, keep it
        else
        {
            currentHumid = roundf(h);
            currentTempF = roundf(f);

            SERIAL.printf("Humid: %d%% - Temp: %d*F ", currentHumid, currentTempF);
            SERIAL.println(Time.timeStr());

            haveValidReading = true;
            digitalWrite(LED_PIN, LOW);

            doAlarmIfNecessary();
        }

        lastReadingMillis = now;
    }
}

static void doReportIfTime(void)
{
    static unsigned long lastReportMillis = 0;
    unsigned long now = millis();
    char publishString[64];

    // Skip report if there hasn't been a valid reading
    if (!haveValidReading) return;

    if ((lastReportMillis == 0) || ((now - lastReportMillis) >= sensorSettings.reportMillis))
    {
        SERIAL.println("Reporting sensor data to server");

        snprintf(publishString, sizeof(publishString), "{\"tempF\": %d, \"humid\": %d}", currentTempF, currentHumid);
        Particle.publish("sensorData", publishString, PRIVATE);

#ifdef USE_ADAFRUIT_IO
        // Send to Adafruit IO
        Adafruit_IO_Feed tempFeed = AIOClient->getFeed("temp-alarm.temperature");
        snprintf(publishString, sizeof(publishString), "%d", currentTempF);
        if (!tempFeed.send(publishString))
        {
            SERIAL.println("Failed to publish temperature to Adafruit IO!");
        }

        // Yield to Particle before sending the humidity
        Particle.process();

        Adafruit_IO_Feed humidityFeed = AIOClient->getFeed("temp-alarm.humidity");
        snprintf(publishString, sizeof(publishString), "%d", currentHumid);
        if (!humidityFeed.send(publishString))
        {
            SERIAL.println("Failed to publish humidity to Adafruit IO!");
        }
#endif

        lastReportMillis = now;
    }
}

void setup(void)
{
    SERIAL.begin(SERIAL_BAUD);

    EEPROM.get(SETTINGS_ADDR, sensorSettings);
    if (SETTINGS_MAGIC_NUM == sensorSettings.magic)
    {
        SERIAL.printlnf("reportMillis = %u", sensorSettings.reportMillis);
        SERIAL.printlnf("AIO Key: %s", sensorSettings.aioKey);
        SERIAL.printlnf("Low Threshold: %d", sensorSettings.lowThres);
        SERIAL.printlnf("High Threshold: %d", sensorSettings.highThres);
    }
    else
    {
        SERIAL.println("EEPROM not programmed! Setting default values.");
        sensorSettings.magic = SETTINGS_MAGIC_NUM;
        sensorSettings.reportMillis = DEFAULT_REPORT_MS;
        // sensorSettings.aioKey intentionally not set
        sensorSettings.lowThres = DEFAULT_LOW_THRES;
        sensorSettings.highThres = DEFAULT_HIGH_THRES;
        EEPROM.put(SETTINGS_ADDR, sensorSettings);
    }

    Particle.variable("currentTempF", currentTempF);
    Particle.variable("currentHumid", currentHumid);
    Particle.variable("lowThres", sensorSettings.lowThres);
    Particle.variable("highThres", sensorSettings.highThres);

    Particle.function("reportRate", setReportRate);
    Particle.function("aioKey", setAIOKey);
    Particle.function("lowThres", setLowThres);
    Particle.function("highThres", setHighThres);
    Particle.function("alarmTest", alarmTest);

    SERIAL.println("Initializing DHT22 sensor");
    dht.begin();
    delay(2000);

#ifdef USE_ADAFRUIT_IO
    AIOClient = new Adafruit_IO_Client(tcpClient, sensorSettings.aioKey);
    AIOClient->begin();
#endif

    pinMode(LED_PIN, OUTPUT);
}

void loop(void)
{
    doMonitorIfTime();
    doReportIfTime();
}
