/*
 * Copyright (C) 2020 Gregory S. Meiste  <http://gregmeiste.com>
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

#define SERIAL             Serial   // USB port
//#define SERIAL           Serial1  // TX/RX pins
#define SERIAL_BAUD        115200

// Not defined by DeviceOS for some reason
#define RGB_COLOR_NONE     0x00000000

LEDStatus blinkRed(RGB_COLOR_RED, LED_PATTERN_BLINK, LED_SPEED_NORMAL, LED_PRIORITY_IMPORTANT);

// Used to setup the system LED theme: disable the breathing cyan on cloud connection
LEDSystemTheme theme;

void sensor_data(const char *event, const char *data)
{
  SERIAL.printlnf("%s: %s", event, data);
  // Maybe someday we'll do more with this data...
}

void temp_alarm(const char *event, const char *data)
{
  SERIAL.printlnf("%s: %s", event, data);
  blinkRed.setActive(true);
}

void button_press(system_event_t event, int param)
{
  SERIAL.printlnf("button_press: param=%d", param);

  // Zero means press, non-zero is duration
  if (param == 0)
  {
    // Priority is to handle the alarm event
    if (blinkRed.isActive())
    {
      blinkRed.setActive(false);
    }
    // Enable/disable breathing cyan if not alarming
    else if (theme.color(LED_SIGNAL_CLOUD_CONNECTED) == RGB_COLOR_NONE)
    {
      theme.setColor(LED_SIGNAL_CLOUD_CONNECTED, RGB_COLOR_CYAN);
      theme.apply();
    }
    else
    {
      theme.setColor(LED_SIGNAL_CLOUD_CONNECTED, RGB_COLOR_NONE);
      theme.apply();
    }
  }
}

void setup() {
  SERIAL.begin(SERIAL_BAUD);
  SERIAL.println("Temperature alarm initializing");

  Particle.subscribe("sensorData", sensor_data, MY_DEVICES);
  Particle.subscribe("tempAlarm", temp_alarm, MY_DEVICES);

  System.on(button_status, button_press);

  theme.setColor(LED_SIGNAL_CLOUD_CONNECTED, RGB_COLOR_NONE);
  theme.apply();
}

void loop() {
  // Nothing to do
}