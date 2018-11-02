//STA ORIGINAL MAC 80:7D:3A:FF:FF:FF {0x80, 0x7D, 0x3A, 0xFF, 0xFF, 0xFF}

#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
//#include <FS.h>
//#include <SPIFFS.h>
#include <math.h>
#include <time.h>
#include <DHT.h>
#include "ca_crt.h" //contains ca certificate
#include "client_hardware_crt.h" //contains client certificate
#include "client_hardware_key.h" //contains client private key
#define DHTTYPE DHT11

uint8_t STAMac[] = {0x80, 0x7D, 0x3A, 0xFF, 0xFF, 0xFF}; //define custom mac address
int wifiConnectCount = 0, mqttConnectCount = 0;
int dhtPin = 15, relayPin1 = 18, relayPin2 = 19, relayPin3 = 22, relayPin4 = 23; //GPIO declearization
float dhtTemp = 0, dhtHum = 0, prevDHTTemp = 0, prevDHTHum = 0;
char temp_char[10], hum_char[10];
String pubTopic, msg;
TaskHandle_t firstCoreTask, secondCoreTask;

String macAddr = WiFi.macAddress();
const char* hostName = "IoT-Node-";// + macAddr.toCharArray(char* charHostName,macAddr.length()+1);
const char* ssid = "Secure WiFi";
const char* password = "12345678";
const char* mqttServer = "mqtt.example.com";
const int mqttPort = 8883;
const char* mqttUser = "user_name";
const char* mqttPassword = "password";
const char* mqttServerFingerprint = "79:B2:83:3F:BD:6D:62:A4:9D:97:E9:FD:C5:6E:E8:7D:75:10:2E:B7:1D:ED:11:67:96:3D:F9:20:9C:7D:8D:C8"; //sha256 fingerprint
const char* ntpServer = "time.google.com";
/*
  IPAddress espIp(192,168,1,10);
  IPAddress espSubnet(255,255,255,0);
  IPAddress espGateway(192,168,1,1);
  IPAddress espDns(192,168,1,1);
*/

void setup_relay();
void get_boardInfo();
void setup_wifi();
void getTime();
void wifiReconnect();
void load_certs();
void verify_tls();
void callback(char*, byte*, unsigned int);
void getDHT();
void publishDHT();
void mqttReconnect();

WiFiClientSecure espClient;
PubSubClient mqttClient(mqttServer, mqttPort, espClient);
DHT dht(dhtPin, DHTTYPE);

void setup_relay()
{
  pinMode(relayPin1, OUTPUT);
  digitalWrite(relayPin1, HIGH);
  pinMode(relayPin2, OUTPUT);
  digitalWrite(relayPin2, HIGH);
  pinMode(relayPin3, OUTPUT);
  digitalWrite(relayPin3, HIGH);
  pinMode(relayPin4, OUTPUT);
  digitalWrite(relayPin4, HIGH);
}

void get_boardInfo()
{
  Serial.println();
  Serial.print("ChipId: ");
  Serial.print(ESP.getChipRevision());
  Serial.print("@");
  Serial.print(ESP.getCpuFreqMHz());
  Serial.println("MHz");
  Serial.print("FlashMode: ");
  Serial.print(ESP.getFlashChipMode());
  Serial.print("@");
  Serial.print(ESP.getFlashChipSpeed() / 1000000);
  Serial.print("MHz, Size: ");
  Serial.print(ESP.getFlashChipSize() / 1024);
  Serial.println("KB");
}

void setup_wifi()
{
  delay(10);
  // We start by connecting to a WiFi network
  WiFi.mode(WIFI_STA);
  esp_wifi_set_mac(ESP_IF_WIFI_STA, STAMac);
  WiFi.setHostname(hostName);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  //WiFi.config(espIp, espSubnet, espGateway, espDns);

  while (WiFi.status() != WL_CONNECTED)
  {
    wifiConnectCount++;
    if (wifiConnectCount >= 200)
      ESP.restart();

    delay(500);
    Serial.print(".");
  }

  wifiConnectCount = 0;

  Serial.println("");
  Serial.println("WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Host Name: ");
  Serial.println(hostName);
}

void getTime()
{
  Serial.println("Setting time using NTP...");
  configTime((5 * 3600), (30 * 60), ntpServer); //timezone in sec, daylightOffset in sec, server_name1, server_name2, server_name3
  delay(1000);

  time_t timeNow;
  time(&timeNow);
  Serial.print("Current time: ");
  Serial.print(ctime(&timeNow));
}

void wifiReconnect()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.reconnect();
    while (WiFi.status() != WL_CONNECTED)
    {
      wifiConnectCount++;
      if (wifiConnectCount >= 100)
        ESP.restart();

      delay(500);
      Serial.print(".");
    }

    wifiConnectCount = 0;

    Serial.println("");
    Serial.println("WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Host Name: ");
    Serial.println(hostName);
    getTime();
  }
}

void load_certs()
{
  // Set server CA file
  try
  {
    espClient.setCACert(ca_crt);
  }
  catch (const char* loadCACertException)
  {
    Serial.println(loadCACertException);
  }

  //Set client cert file
  try
  {
    espClient.setCertificate(client_hardware_crt);
  }
  catch (const char* loadCertificateException)
  {
    Serial.println(loadCertificateException);
  }

  //Set client key file
  try
  {
    espClient.setPrivateKey(client_hardware_key);
  }
  catch (const char* loadKeyException)
  {
    Serial.println(loadKeyException);
  }
}

void verify_tls()
{
  // Use WiFiClientSecure class to create TLS connection
  Serial.print("connecting to ");
  Serial.println(mqttServer);
  if (!espClient.connect(mqttServer, mqttPort))
  {
    Serial.println("connection failed");
    return;
  }

  if (espClient.verify(mqttServerFingerprint, mqttServer))
  {
    Serial.println("Fingerprint matches!");
  }
  else
  {
    Serial.println("Fingerprint doesn't match!");
  }
}

void callback(char* topic, byte* payload, unsigned int length)
{
  pubTopic = String(topic);
  msg = "";
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  Serial.println(msg);
  if (espClient.verify(mqttServerFingerprint, mqttServer))
  {
    if (pubTopic == "serverRoom/switches/bulb1")
    {
      if (msg == "on")
      {
        digitalWrite(relayPin1, LOW);
      }
      else if (msg == "off")
      {
        digitalWrite(relayPin1, HIGH);
      }
      return;
    }

    if (pubTopic == "serverRoom/switches/bulb2")
    {
      if (msg == "on")
      {
        digitalWrite(relayPin2, LOW);
      }
      else if (msg == "off")
      {
        digitalWrite(relayPin2, HIGH);
      }
      return;
    }

    if (pubTopic == "serverRoom/switches/bulb3")
    {
      if (msg == "on")
      {
        digitalWrite(relayPin3, LOW);
      }
      else if (msg == "off")
      {
        digitalWrite(relayPin3, HIGH);
      }
      return;
    }

    if (pubTopic == "serverRoom/switches/bulb4")
    {
      if (msg == "on")
      {
        digitalWrite(relayPin4, LOW);
      }
      else if (msg == "off")
      {
        digitalWrite(relayPin4, HIGH);
      }
      return;
    }
  }
}

void setup()
{
  setup_relay();
  Serial.begin(115200);
  get_boardInfo();
  setup_wifi();
  getTime();
  load_certs();
  verify_tls();
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(callback);
  dht.begin();

  //create a task that will be executed in the firstCoreTaskLoop() function, with priority 1 and executed on core 0
  //(Task function, name of task, Stack size of task, parameter of the task, priority of the task, Task handle to keep track of created task, pin task to core 0)
  xTaskCreatePinnedToCore(firstCoreTaskLoop, "firstCoreTask", 10240, NULL, 1, &firstCoreTask, 0);
  delay(500);

  //create a task that will be executed in the secondCoreTaskLoop() function, with priority 1 and executed on core 1
  //(Task function, name of task, Stack size of task, parameter of the task, priority of the task, Task handle to keep track of created task, pin task to core 1)
  xTaskCreatePinnedToCore(secondCoreTaskLoop, "secondCoreTask", 10240, NULL, 1, &secondCoreTask, 1);
  delay(500);
}

void getDHT()
{
  float tempDHTTemp = dhtTemp;
  float tempDHTHum = dhtHum;
  dhtTemp = dht.readTemperature();
  dhtHum = dht.readHumidity();
  if ((isnan(dhtTemp)) || (isnan(dhtHum)))   // Check if any reads failed and exit early (to try again).
  {
    dhtTemp = tempDHTTemp;
    dhtHum = tempDHTHum;
    return;
  }
}

void publishDHT()
{
  if ((prevDHTTemp != dhtTemp) || (prevDHTHum != dhtHum))
  {
    prevDHTTemp = dhtTemp;
    prevDHTHum = dhtHum;
    Serial.print(dhtTemp);
    Serial.print(",");
    Serial.println(dhtHum);
    dtostrf(dhtTemp, 0, 0, temp_char);
    dtostrf(dhtHum, 0, 0, hum_char);
    if (espClient.verify(mqttServerFingerprint, mqttServer))
    {
      mqttClient.publish("serverRoom/sensors/temperature", temp_char);
      mqttClient.publish("serverRoom/sensors/humidity", hum_char);
      delay(500);
    }
    else
      Serial.println("Fingerprint doesn't match!");
  }
}

void mqttReconnect()
{
  // Loop until we're reconnected
  while (!mqttClient.connected())
  {
    mqttConnectCount++;
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqttClient.connect("Hexatron-IoT-Node1", mqttUser, mqttPassword))
    {
      mqttConnectCount = 0;
      Serial.println("connected");
      getTime();
      if (espClient.verify(mqttServerFingerprint, mqttServer))
      {
        mqttClient.subscribe("serverRoom/switches/bulb1");
        mqttClient.subscribe("serverRoom/switches/bulb2");
        mqttClient.subscribe("serverRoom/switches/bulb3");
        mqttClient.subscribe("serverRoom/switches/bulb4");
      }
      else
        Serial.println("Fingerprint doesn't match!");
    }
    else
    {
      if (mqttConnectCount >= 10)
        ESP.restart();

      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void firstCoreTaskLoop(void* pvParameters)
{
  while (1)
  {
    getDHT();

    delay(500);
  }
}

void secondCoreTaskLoop(void* pvParameters)
{
  while (1)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      wifiReconnect();
    }
    else
    {
      if (!mqttClient.connected())
      {
        mqttReconnect();
      }
      else
      {
        mqttClient.loop();
        publishDHT();
      }
    }
  }
}

void loop()
{ //we put our loop code into the secondCoreTaskLoop function so that our loop code executes in cpu1
}
