/**
 * @file main.cpp
 * @author aan
 * @brief Prototipe Reksti smart room lighting untuk pemantauan lansia. Program akan menerima input sensor pir dan loadcell 50kg. Jika sensor pir mendeteksi gerakan, maka lampu akan menyala. Jika loadcell mendeteksi beban lebih dari 50kg, maka lampu akan menyala. Jika kedua sensor mendeteksi, maka lampu akan menyala. Program akan mengirimkan data lampu menyala/mati ke server melalui wifi. Program akan menerima data dari server. Program akan melakukan kalibrasi beban saat pertama kali dinyalakan
 * @version 0.1
 * @date 2024-05-14
 *
 * @copyright Copyright (c) 2024
 *
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HX711_ADC.h>

#if defined(ESP8266) || defined(ESP32) || defined(AVR)
#include <EEPROM.h>
#endif

#define SSID "asweh"
#define PASSWORD "abcdefgh"
#define SERVER_HOST "https://simanis.stei.itb.ac.id/aan/api"
#define DEVICE_ID "bukanhans2"

#define PIR_SENSOR 27
#define LOADCELL_DOUT 26
#define LOADCELL_SCK 25
#define RELAY 14
#define LED_BUTTON 33

#define LOADCELL_THRESHOLD 35
#define DEFAULT_LIGHT_STATE false
#define LIGHT_HOLD_TIME 10 * 1000

HX711_ADC loadcell(LOADCELL_DOUT, LOADCELL_SCK);

const int calVal_eepromAdress = 0x00;
const int tareOffset_eepromAdress = 0x10;
float calVal = 1;
float tareOffset = 0;

float loadcell_data = 0;

int light_hold_timer = 0;
int previous_light_state = DEFAULT_LIGHT_STATE;

boolean new_data = false;

struct Data
{
  boolean light;
  boolean pir;
  float_t load;
  boolean override;
};

Data data;

Data create_data(boolean pir, float_t load, boolean override)
{
  Data data;
  data.light = load > LOADCELL_THRESHOLD ? false : pir ? true
                                                       : false;
  data.pir = pir;
  data.load = load;
  data.override = override;
  return data;
}

boolean send_data(Data data)
{
  HTTPClient http;
  http.begin(String(SERVER_HOST) + "/data?device_id=" + DEVICE_ID);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST("{\"light\": " + String(data.light) + ", \"pir\": " + String(data.pir) + ", \"load\": " + String(boolean(data.load > LOADCELL_THRESHOLD)) + ", \"override\": " + String(data.override) + "}");
  if (httpCode > 0)
  {
    String payload = http.getString();
    Serial.println(payload);
    return true;
  }
  else
  {
    Serial.println("Error on HTTP request");
    return false;
  }
  http.end();
}

void calibrate()
{
  Serial.println("***");
  Serial.println("Start calibration:");
  Serial.println("Place the load cell an a level stable surface.");
  Serial.println("Remove any load applied to the load cell.");
  Serial.println("Send 't' from serial monitor to set the tare offset.");

  boolean _resume = false;
  while (_resume == false)
  {
    loadcell.update();
    if (Serial.available() > 0)
    {
      if (Serial.available() > 0)
      {
        char inByte = Serial.read();
        if (inByte == 't')
          loadcell.tareNoDelay();
      }
    }
    if (loadcell.getTareStatus() == true)
    {
      Serial.println("Tare complete");
      _resume = true;
    }
  }

  Serial.println("Now, place your known mass on the loadcell.");
  Serial.println("Then send the weight of this mass (i.e. 100.0) from serial monitor.");

  float known_mass = 0;
  _resume = false;
  while (_resume == false)
  {
    loadcell.update();
    if (Serial.available() > 0)
    {
      known_mass = Serial.parseFloat();
      if (known_mass != 0)
      {
        Serial.print("Known mass is: ");
        Serial.println(known_mass);
        _resume = true;
      }
    }
  }

  loadcell.refreshDataSet();                                          // refresh the dataset to be sure that the known mass is measured correct
  float newCalibrationValue = loadcell.getNewCalibration(known_mass); // get the new calibration value
  tareOffset = loadcell.getTareOffset();                              // get the new tare offset

  Serial.print("New calibration value has been set to: ");
  Serial.print(newCalibrationValue);

  EEPROM.begin(512);
  EEPROM.put(calVal_eepromAdress, newCalibrationValue);
  EEPROM.put(tareOffset_eepromAdress, tareOffset);
  EEPROM.commit();
  EEPROM.get(calVal_eepromAdress, newCalibrationValue);
  EEPROM.get(tareOffset_eepromAdress, tareOffset);
  EEPROM.end();

  Serial.print("Value ");
  Serial.print(newCalibrationValue);
  Serial.print(" saved to EEPROM address: ");
  Serial.println(calVal_eepromAdress);
  Serial.print("Tare offset ");
  Serial.print(tareOffset);
  Serial.print(" saved to EEPROM address: ");

  Serial.println("End calibration");
  Serial.println("***");
  Serial.println("To re-calibrate, send 'r' from serial monitor.");
  Serial.println("For manual edit of the calibration value, send 'c' from serial monitor.");
  Serial.println("***");
}

void setup()
{
  Serial.begin(115200);
  pinMode(PIR_SENSOR, INPUT);
  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, LOW);
  pinMode(LED_BUTTON, INPUT);
  loadcell.begin();
  unsigned long stabilizingtime = 2000; // preciscion right after power-up can be improved by adding a few seconds of stabilizing time
  boolean _tare = true;                 // set this to false if you don't want tare to be performed in the next step
  if (loadcell.getTareTimeoutFlag() || loadcell.getSignalTimeoutFlag())
  {
    Serial.println("Timeout, check MCU>HX711 wiring and pin designations");
    while (1)
      ;
  }
  else
  {
    loadcell.setCalFactor(1.0); // user set calibration value (float), initial value 1.0 may be used for this sketch
    Serial.println("Startup is complete");
  }

  while (!loadcell.update())
    ;

  EEPROM.begin(512);
  EEPROM.get(calVal_eepromAdress, calVal);
  EEPROM.get(tareOffset_eepromAdress, tareOffset);

  if (calVal == 0 || tareOffset == 0)
  {
    calibrate();
  }
  else
  {
    loadcell.setCalFactor(calVal);
    loadcell.setTareOffset(tareOffset);
  }

  EEPROM.end();

  Serial.println("Calibration factor: " + String(calVal));
  Serial.println("Tare offset: " + String(tareOffset));

  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println("Connected to the WiFi network");

  Serial.println("Setup done");

  light_hold_timer = millis();
}

void loop()
{
  if (loadcell.update())
  {
    loadcell_data = loadcell.getData();
    printf("Loadcell data updated:");
  }
  else
  {
    printf("Loadcell data not updated:");
  }

  data = create_data(digitalRead(PIR_SENSOR), loadcell_data, 0);

  printf("\tLoadcell: %f", data.load);
  printf("\tPIR: %d", digitalRead(PIR_SENSOR));
  printf("\tLight: %d", data.light);

  if (data.light)
  {
    if (!previous_light_state)
    {
      new_data = true;
      previous_light_state = true;
    }

    digitalWrite(RELAY, HIGH);
    light_hold_timer = millis();
    printf("\tLampu menyala");
  }
  else if (millis() - light_hold_timer > LIGHT_HOLD_TIME)
  {
    if (previous_light_state)
    {
      new_data = true;
      previous_light_state = false;
    }

    digitalWrite(RELAY, LOW);
    light_hold_timer = millis();
    printf("\tLampu mati");
  }

  if (new_data)
  {
    if (send_data(data))
    {
      printf("\tData sent to server");
    }
    else
    {
      printf("\tData not sent to server");
    }

    new_data = false;
  }

  printf("\n");

  if (Serial.available() > 0)
  {
    char inByte = Serial.read();
    if (inByte == 't')
      loadcell.tareNoDelay(); // tare
    else if (inByte == 'r')
      calibrate(); // calibrate
  }

  delay(100);
}
