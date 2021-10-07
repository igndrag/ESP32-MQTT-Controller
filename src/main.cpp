#include <SPI.h>
#include <MFRC522.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <Hash.h>
#include <FS.h>
#else
#include <WiFi.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#endif
#if defined(ESP8266)
#include <ESP8266WebServer.h>
#else
#include <WebServer.h>
#endif
#include <PubSubClient.h>
//#include <CircularBuffer.h>
#include <ESPAsyncWebServer.h>
//include <webInterface.cpp>
//#include <Arduino_JSON.h>

#define SO_0 GPIO_NUM_33
#define SO_1 GPIO_NUM_25
#define SO_2 GPIO_NUM_26
#define SO_3 GPIO_NUM_27
#define SO_4 GPIO_NUM_14
#define SO_5 GPIO_NUM_12

#define CONFIG_MODE_INPUT GPIO_NUM_32

#define L_SS_PIN 2
#define L_RST_PIN 16
#define L_IRQ_PIN 4

#define R_SS_PIN 21
#define R_RST_PIN 22
#define R_IRQ_PIN 17

#define STATUS_LED_PIN 13

const char* DEFAULT_MODULE_ID = "SC";
const int DEFAULT_BROKER_PORT = 1883;
const char* DEFAULT_BROKER_PORT_STR = "1883";
const char* DEFAULT_BROKER_IP = "192.168.1.34";
const char* DEFAULT_SSID = "NATrain";
const char* DEFAULT_PASSWORD = "GoodLock";

AsyncWebServer server(80);

boolean toConfigInterrupt = false;

// REPLACE WITH YOUR NETWORK CREDENTIALS
const char* hotspotSsid = "ModuleWebInterface";
const char* hotspotPassword = "GoodLock";

const char* PARAM_MODULE_ID = "moduleId";
const char* PARAM_BROKER_IP = "brokerIp";
const char* PARAM_BROKER_PORT = "brokerPort";
const char* PARAM_SSID = "brokerSsid";
const char* PARAM_PASSWORD = "brokerPassword";
const char* PARAM_RESET = "reset";

volatile boolean configMode = false;
volatile boolean initialize = true;

volatile int tripHoldingTime = 2000; // millis

volatile long leftReaderInterruptMoment = 0;
volatile long leftRaderTrippingMoment = LONG_MAX - tripHoldingTime;

volatile long rightReaderInterruptMoment = 0;
volatile long rightReaderTrippingMoment = LONG_MAX - tripHoldingTime;

MFRC522 leftReader(L_SS_PIN, L_RST_PIN);  // Instance of the class
MFRC522 rightReader(R_SS_PIN, R_RST_PIN); // Instance of the class

MFRC522::MIFARE_Key key;

volatile byte previousLeftTag[4] = {0xff, 0xff, 0xff, 0xfa};
volatile byte previousRightTag[4] = {0xff, 0xff, 0xff, 0xfb};
volatile byte lastLeftTag[4] = {0xff, 0xff, 0xff, 0xfc};
volatile byte lastRightTag[4] = {0xff, 0xff, 0xff, 0xfd};

volatile int approvedTagsCount = 0;
uint8_t **approvedTags;

const int OK_STATUS_CODE = 1;
const int READER_ERROR_STATUS_CODE = 2;

const int TO_CONFIG_MODE_COMMAND_CODE = 95;
const int SELF_STATUS_REQUEST_COMMAND_CODE = 96;
const int TEST_MODE_COMMAND_CODE = 97;
const int SET_CONFIGS_COMMAND_CODE = 98;
const int GLOBAL_REQUEST_COMMAND_CODE = 99;

const int NOT_LIGHT_CODE = 0;
const int LIGHT_CODE = 1;
const int BLINKING_CODE = 2;

volatile boolean inputStates[6];
int outputStatusCodes[6];

String id = "TestModule";
String subscriptionRoot = "";
String publishRoot = "";

String brokerSsid = "NATrain";
String brokerPassword = "GoodLock";
String brokerIp = "192.168.1.34";
int   brokerPort = 1883;
const char* commandsTopicRoot = "NATrain/controlModules/commands/";
const char* responsesTopicRoot = "NATrain/controlModules/responses/";
const char* systemMessageRoot = "NATrain/system";

volatile long publishMoment = 0;
boolean leftChecked = false;
boolean rightChecked = false;

WiFiClient espClient;
PubSubClient client(espClient); //lib required for mqtt

xSemaphoreHandle rightSemaphorHandler;
xSemaphoreHandle leftSemaphorHandler;
xSemaphoreHandle configSemaphorHandler;

volatile long leftReaderLastTagSaving = 0;
volatile long rightReaderLastTagSaving = 0;
const int LAST_TAG_KEEPING_TIME = 500;

void(* resetFunc) (void) = 0;

int getOutputCannel(int chNumber)
{
  switch (chNumber)
  {
  case 0:
    return SO_0;
  case 1:
    return SO_1;
  case 2:
    return SO_2;
  case 3:
    return SO_3;
  case 4:
    return SO_4;
  case 5:
    return SO_5;
  default:
    return -1;
  }
}

void printHex(byte *buffer, byte bufferSize)
{
  for (byte i = 0; i < bufferSize; i++)
  {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
  Serial.println();
}

boolean isTagApproved(volatile byte *tag)
{
  if (approvedTags == NULL)
    return true;
  for (int i = 0; i < approvedTagsCount; i++)
  {
    if (tag[0] == approvedTags[i][0] &&
        tag[1] == approvedTags[i][1] &&
        tag[2] == approvedTags[i][2] &&
        tag[3] == approvedTags[i][3])
    {
      return true;
    }
  }
  return false;
}

unsigned long convertToDec(volatile byte *buffer, byte bufferSize)
{
  unsigned long decUid = 0;
  decUid += (unsigned long)buffer[0] << 24;
  decUid += (unsigned long)buffer[1] << 16;
  decUid += (long)buffer[2] << 8;
  decUid += (long)buffer[3];
  return decUid;
}

void publishSelfStatusRequest()
{
  String response;
  response += id;
  response += ":";
  response += SELF_STATUS_REQUEST_COMMAND_CODE;
  client.publish(publishRoot.c_str(), response.c_str());
}

void reconnect()
{
  while (!client.connected() && configMode == false )
  {
    Serial.println("Attempting MQTT connection...");
    if (configMode == false) {
    if (client.connect(id.c_str()))
    {
      Serial.println("connected");
      initialize = false;
      // Once connected, publish an announcement...
      //client.publish(systemMessageRoot, (id + " connected").c_str());
      // ... and resubscribe
      client.subscribe(subscriptionRoot.c_str());
      client.subscribe(systemMessageRoot);
      Serial.print("subscribed to : ");
      Serial.println(subscriptionRoot);
      publishSelfStatusRequest();
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      // Wait 5 seconds before retrying
      delay(2000);
    }
    }
  }
}

void leftTagReadersInterruptHandlingTask(void *pvParameters)
{
  while (initialize)
  {
    vTaskDelay(100 / portTICK_RATE_MS);
  }
  Serial.println("Left reader Interrupt handler task started");

  leftReader.PCD_WriteRegister(leftReader.ComIrqReg, 0x7F);
  leftReader.PCD_WriteRegister(leftReader.FIFODataReg, leftReader.PICC_CMD_REQA);
  leftReader.PCD_WriteRegister(leftReader.CommandReg, leftReader.PCD_Transceive);
  leftReader.PCD_WriteRegister(leftReader.BitFramingReg, 0x87);
  vTaskDelay(20 / portTICK_RATE_MS);

  for (;;)
  {
    leftReader.PCD_WriteRegister(leftReader.FIFODataReg, leftReader.PICC_CMD_REQA);
    leftReader.PCD_WriteRegister(leftReader.CommandReg, leftReader.PCD_Transceive);
    leftReader.PCD_WriteRegister(leftReader.BitFramingReg, 0x87);
    if (xSemaphoreTake(leftSemaphorHandler, 20 * portTICK_PERIOD_MS) == pdTRUE)
    {
      volatile long handlingMoment = millis();
      if (digitalRead(L_IRQ_PIN) == 0)
      { //Serial.println("LInt");
        // if interrupt still active
        //long leftReading = millis();
        //Serial.print("Reading: ");
        
        MFRC522::StatusCode result = leftReader.PICC_Select(&leftReader.uid);
        //Serial.printf("LR: %d time: %lu\n", result, millis() - handlingMoment);
        if (result == MFRC522::StatusCode::STATUS_OK)
        { // it needs <= 40ms
          //ignore empty readings
          volatile boolean emptyUID = leftReader.uid.uidByte[0] == 0x00 &&
                                      leftReader.uid.uidByte[1] == 0x00 &&
                                      leftReader.uid.uidByte[2] == 0x00 &&
                                      leftReader.uid.uidByte[3] == 0x00;

          volatile boolean newTag = handlingMoment > leftReaderLastTagSaving + LAST_TAG_KEEPING_TIME ||
                                    leftReader.uid.uidByte[0] != lastLeftTag[0] ||
                                    leftReader.uid.uidByte[1] != lastLeftTag[1] ||
                                    leftReader.uid.uidByte[2] != lastLeftTag[2] ||
                                    leftReader.uid.uidByte[3] != lastLeftTag[3];

          volatile boolean stillHolding = (leftReader.uid.uidByte[0] == previousLeftTag[0]) &&
                                          (leftReader.uid.uidByte[1] == previousLeftTag[1]) &&
                                          (leftReader.uid.uidByte[2] == previousLeftTag[2]) &&
                                          (leftReader.uid.uidByte[3] == previousLeftTag[3]) &&
                                          (handlingMoment < (leftRaderTrippingMoment + tripHoldingTime));
          //Serial.print("Left tripped: ");
          //Serial.printf("%X %X %X %X empty: %o \n", leftReader.uid.uidByte[0], leftReader.uid.uidByte[1], leftReader.uid.uidByte[2], leftReader.uid.uidByte[3], emptyUID);

          if (!emptyUID && newTag && !stillHolding)
          {
            leftRaderTrippingMoment = handlingMoment;
            leftReaderLastTagSaving = millis();
            // Store NUID into nuidPICC array
            for (byte i = 0; i < 4; i++)
            {
              lastLeftTag[i] = leftReader.uid.uidByte[i];
            }

            if (isTagApproved(lastLeftTag))
            {
              //Serial.write("LR: ");
              String response = id + ":1:";
              response += convertToDec(lastLeftTag, 4);
              //long currentmoment = millis();
              //Serial.println(response);
              if (client.connected())
              {
                client.publish(publishRoot.c_str(), response.c_str());
                //vTaskDelay(5 / portTICK_RATE_MS); // sleep 100ms before switch to receive mode
              }
            }
            else
            {
              Serial.write("LR: not approved tag!\n");
            }

            if (lastLeftTag[0] == lastRightTag[0] &&
                lastLeftTag[1] == lastRightTag[1] &&
                lastLeftTag[2] == lastRightTag[2] &&
                lastLeftTag[3] == lastRightTag[3])
            {
              for (byte i = 0; i < 4; i++)
              {
                previousRightTag[i] = lastRightTag[i];
                lastRightTag[i] = 0xFF;
              }
            }
          }
          //Serial.printf("OK %lu\n", millis() - leftReading);
        }
        leftReader.PCD_WriteRegister(leftReader.ComIrqReg, 0x7F); //clear interrupt
        leftReader.PICC_HaltA();
      }
    }
  }
}

void rightTagReadersInterruptHandlingTask(void *pvParameters)
{
  while (initialize)
  {
    vTaskDelay(100 / portTICK_RATE_MS);
  }
  Serial.println("Right reader interrupt handler task started");
  rightReader.PCD_WriteRegister(rightReader.ComIrqReg, 0x7F);
  rightReader.PCD_WriteRegister(rightReader.FIFODataReg, rightReader.PICC_CMD_REQA);
  rightReader.PCD_WriteRegister(rightReader.CommandReg, rightReader.PCD_Transceive);
  rightReader.PCD_WriteRegister(rightReader.BitFramingReg, 0x87);
  vTaskDelay(20 / portTICK_RATE_MS);

  for (;;)
  {
    rightReader.PCD_WriteRegister(rightReader.FIFODataReg, rightReader.PICC_CMD_REQA);
    rightReader.PCD_WriteRegister(rightReader.CommandReg, rightReader.PCD_Transceive);
    rightReader.PCD_WriteRegister(rightReader.BitFramingReg, 0x87);
    if (xSemaphoreTake(rightSemaphorHandler, 20 * portTICK_PERIOD_MS) == pdTRUE)
    {
      volatile long handlingMoment = millis();
      if (digitalRead(R_IRQ_PIN) == 0) // if interrupt still active
      {
        //Serial.println("RInt");
        MFRC522::StatusCode result = rightReader.PICC_Select(&rightReader.uid);
        //Serial.printf("RR:%d  time: %lu\n", result, millis() - handlingMoment);
        if (result == MFRC522::StatusCode::STATUS_OK)
        {
          //ignore empty readings
          volatile boolean emptyUID = rightReader.uid.uidByte[0] == 0x00 &&
                                      rightReader.uid.uidByte[1] == 0x00 &&
                                      rightReader.uid.uidByte[2] == 0x00 &&
                                      rightReader.uid.uidByte[3] == 0x00;

          volatile boolean newTag = handlingMoment > rightReaderLastTagSaving + LAST_TAG_KEEPING_TIME ||
                                    rightReader.uid.uidByte[0] != lastRightTag[0] ||
                                    rightReader.uid.uidByte[1] != lastRightTag[1] ||
                                    rightReader.uid.uidByte[2] != lastRightTag[2] ||
                                    rightReader.uid.uidByte[3] != lastRightTag[3];

          volatile boolean stillHolding = (rightReader.uid.uidByte[0] == previousRightTag[0]) &&
                                          (rightReader.uid.uidByte[1] == previousRightTag[1]) &&
                                          (rightReader.uid.uidByte[2] == previousRightTag[2]) &&
                                          (rightReader.uid.uidByte[3] == previousRightTag[3]) &&
                                          (handlingMoment < (rightReaderTrippingMoment + tripHoldingTime));

          //Serial.printf("Readed value %X %X %X %X \n", rightReader.uid.uidByte[0], rightReader.uid.uidByte[1], rightReader.uid.uidByte[2], rightReader.uid.uidByte[3]);
          //Serial.printf("Prevoiuse tag value %X %X %X %X \n", previousRightTag[0], previousRightTag[1], previousRightTag[2], previousRightTag[3]);
          //Serial.printf("Tripping moment: %lu Handling moment: %lu Difference: %lu \n", rightReaderTrippingMoment, handlingMoment, handlingMoment - rightReaderTrippingMoment);

          if (!emptyUID && newTag && !stillHolding)
          {
            rightReaderTrippingMoment = handlingMoment;
            rightReaderLastTagSaving = millis();
            // Store NUID into nuidPICC array

            for (byte i = 0; i < 4; i++)
            {
              lastRightTag[i] = rightReader.uid.uidByte[i];
            }
            //Serial.printf("Saved tag value %X %X %X %X \n", lastRightTag[0], lastRightTag[1], lastRightTag[2], lastRightTag[3]);
            //Serial.printf("Previouse tag value %X %X %X %X \n", lastRightTag[0], previousRightTag[1], previousRightTag[2], previousRightTag[3]);

            if (isTagApproved(lastRightTag))
            {
              //Serial.write("RR: ");
              String response = id + ":0:";
              response += convertToDec(lastRightTag, 4);
              response += " ";
              //long currentmoment = millis();
              //Serial.println(response);
              if (client.connected())
              {
                client.publish(publishRoot.c_str(), response.c_str());
                //vTaskDelay(5 / portTICK_RATE_MS); // sleep 100ms before switch to receive mode
              }
            }
            else
            {
              Serial.write("RR: not approved tag!\n");
            }

            if (lastRightTag[0] == lastLeftTag[0] &&
                lastRightTag[1] == lastLeftTag[1] &&
                lastRightTag[2] == lastLeftTag[2] &&
                lastRightTag[3] == lastLeftTag[3])
            {
              for (byte i = 0; i < 4; i++)
              {
                previousLeftTag[i] = lastLeftTag[i];
                lastLeftTag[i] = 0xFF;
              }
            }
          }
        }
        rightReader.PCD_WriteRegister(rightReader.ComIrqReg, 0x7F); //clear interrupt
        rightReader.PICC_HaltA();
      }
    }
  }
}

void checkRightReader()
{
  xSemaphoreGive(rightSemaphorHandler);
}

void checkLeftReader()
{
  xSemaphoreGive(leftSemaphorHandler);
}

void blinkTask(void *pvParameters)
{ //blink signal lamps with blinking status codes every 1 second
  for (;;)
  {
    if (configMode != true)
    {
      digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
      for (int i = 0; i < 6; i++)
      {
        if (outputStatusCodes[i] == BLINKING_CODE)
        {
          int outputChannel = getOutputCannel(i);
          digitalWrite(outputChannel, !digitalRead(outputChannel));
        }
      }
    }
    vTaskDelay(1000 / portTICK_RATE_MS);
  }
}

void configurePins()
{
  pinMode(CONFIG_MODE_INPUT, INPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(SO_0, OUTPUT);
  pinMode(SO_1, OUTPUT);
  pinMode(SO_2, OUTPUT);
  pinMode(SO_3, OUTPUT);
  pinMode(SO_4, OUTPUT);
  pinMode(SO_5, OUTPUT);
}

void fillTag(int tagNum, byte *tag)
{
  for (int i = 0; i < 4; i++)
  {
    approvedTags[tagNum][i] = tag[i];
  }
}

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);
  File file = fs.open(path, "r");
  if(!file || file.isDirectory()){
    Serial.println("- empty file or failed to open file");
    return String();
  }
  Serial.println("- read from file:");
  String fileContent;
  while(file.available()){
    fileContent+=String((char)file.read());
  }
  Serial.println(fileContent);
  return fileContent;
}

void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);
  File file = fs.open(path, "w");
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
}

// Replaces placeholder with stored values
String processor(const String& var) {
  //Serial.println(var);
  if(!strcmp(var.c_str(), PARAM_MODULE_ID)){
    return readFile(SPIFFS, "/id.txt");
  } else if(!strcmp(var.c_str(), PARAM_BROKER_IP)){
    return readFile(SPIFFS, "/ip.txt");
  } else if (!strcmp(var.c_str(), PARAM_BROKER_PORT)){
    return readFile(SPIFFS, "/port.txt");
  } else if (!strcmp(var.c_str(), PARAM_SSID)){
    return readFile(SPIFFS, "/ssid.txt");}
    else if (!strcmp(var.c_str(), PARAM_PASSWORD)){
    return readFile(SPIFFS, "/password.txt");}
  return String();
}

void configModeInit() {
  digitalWrite(STATUS_LED_PIN, HIGH);
  configMode = true;
  delay(500);
  if (client.connected()) {
    client.disconnect();
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
  }
  Serial.println("Config mode!");
  //localIp.fromString(defaultIp);
  //WiFi.config(localIp, gateway, subnet); 
  //WiFi.mode(WIFI_MODE_APSTA);
  //WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(hotspotSsid, hotspotPassword);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
 // Send web page with input fields to client
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });

  server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/logo.png", "data");
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/style.css", "text/css");
  });

  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
   
    if (request->hasParam(PARAM_MODULE_ID)) {
      inputMessage = request->getParam(PARAM_MODULE_ID)->value();
      writeFile(SPIFFS, "/id.txt", inputMessage.c_str());
    } else if (request->hasParam(PARAM_BROKER_IP)) {
      inputMessage = request->getParam(PARAM_BROKER_IP)->value();
      writeFile(SPIFFS, "/ip.txt", inputMessage.c_str());
    } else if (request->hasParam(PARAM_BROKER_PORT)) {
      inputMessage = request->getParam(PARAM_BROKER_PORT)->value();
      writeFile(SPIFFS, "/port.txt", inputMessage.c_str());
    } else if (request->hasParam(PARAM_SSID)) {
      inputMessage = request->getParam(PARAM_SSID)->value();
      writeFile(SPIFFS, "/ssid.txt", inputMessage.c_str());
    } else if (request->hasParam(PARAM_PASSWORD)) {
      inputMessage = request->getParam(PARAM_PASSWORD)->value();
      writeFile(SPIFFS, "/password.txt", inputMessage.c_str());
    } else if (request->hasParam(PARAM_RESET)) {
      WiFi.softAPdisconnect(true);
      resetFunc();
    } else {
      inputMessage = "No message sent";
    }
    Serial.println(inputMessage);
    request->send(200, "text/text", inputMessage);
  });
  server.onNotFound(notFound);
  server.begin();
}

void checkConfigMode() {
  xSemaphoreGive(configSemaphorHandler);
}

void checkConfigModeTask(void *pvParameters)
{ //blink signal lamps with blinking status codes every 1 second
  Serial.println("Config button checker started");
  for (;;)
  {
    if (xSemaphoreTake(configSemaphorHandler, portMAX_DELAY) == pdTRUE) {
    configModeInit();
    vTaskSuspend(NULL);  
    }
  }
}

void initConfigs() {
 if(!SPIFFS.open("/id.txt", "r")){
    writeFile(SPIFFS, "/id.txt", DEFAULT_MODULE_ID);
    id = String (DEFAULT_MODULE_ID);
  } else {
    id = String (readFile(SPIFFS, "/id.txt"));
  }

  if(!SPIFFS.open("/ip.txt", "r")){
    writeFile(SPIFFS, "/ip.txt", DEFAULT_BROKER_IP);
    brokerIp = String(DEFAULT_BROKER_IP);
  } else {
    brokerIp = String(readFile(SPIFFS, "/ip.txt"));
  }

  if(!SPIFFS.open("/port.txt", "r")){
    writeFile(SPIFFS, "/port.txt", DEFAULT_BROKER_PORT_STR);
    brokerPort = DEFAULT_BROKER_PORT;
  } else {
    brokerPort = atoi(readFile(SPIFFS, "/port.txt").c_str());
  }
  
  if(!SPIFFS.open("/ssid.txt", "r")){
    writeFile(SPIFFS, "/ssid.txt", DEFAULT_SSID);
    brokerSsid = String(DEFAULT_SSID);
  } else {
    brokerSsid = String(readFile(SPIFFS, "/ssid.txt"));
  }

  if(!SPIFFS.open("/password.txt", "r")){
    writeFile(SPIFFS, "/password.txt", DEFAULT_PASSWORD);
    brokerPassword = String(DEFAULT_PASSWORD);
  } else {
    brokerPassword = String(readFile(SPIFFS, "/password.txt"));
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{ //callback includes topic and payload ( from which (topic) the payload is comming)
  char *fullCommand = (char *)payload;
  int channelNumber;
  int commandType;
  if (strcmp(topic, systemMessageRoot) == 0)
  { //function provides 0 when strings are same!
    char *command = (char *)malloc(5);
    command = strtok_r(fullCommand, "_", &fullCommand);
    sscanf(command, "%02d:%03d", &channelNumber, &commandType);
    if (channelNumber == SET_CONFIGS_COMMAND_CODE)
    {
      approvedTagsCount = commandType;
      approvedTags = (uint8_t **)malloc(approvedTagsCount * sizeof(uint8_t *));
      for (uint8_t i = 0; i < approvedTagsCount; i++)
      {
        approvedTags[i] = (uint8_t *)malloc(4);
      }

      char *tag = (char *)malloc;
      char *endPointer;
      int tagNum = 0;
      for (int i = 0; i < approvedTagsCount; i++)
      {
        tag = strtok_r(NULL, ":", &fullCommand);
        unsigned long tagDecValue = strtoul(tag, &endPointer, 10);
        approvedTags[tagNum][0] = (int)((tagDecValue >> 24) & 0xFF);
        approvedTags[tagNum][1] = (int)((tagDecValue >> 16) & 0xFF);
        approvedTags[tagNum][2] = (int)((tagDecValue >> 8) & 0XFF);
        approvedTags[tagNum][3] = (int)((tagDecValue & 0XFF));
        tagNum++;
      }
      Serial.println("Approved tags:");
      for (int i = 0; i < approvedTagsCount; i++)
      {
        printHex(approvedTags[i], 4);
      }
      return;
    }
  } else
  {
    int commandsCount = length / 6;
    char *command = (char *)malloc(5);
    command = strtok_r(fullCommand, "_", &fullCommand);
    int channelNumber;
    int commandType;
    //Serial.printf("Message length: %d\n", length);
    //Serial.printf("Commands count: %d\n", commandsCount);
    for (int i = 0; i < commandsCount; i++) // USE scanf() here
    {
      //Serial.println(command);
      sscanf(command, "%02d:%02d", &channelNumber, &commandType);
      // Serial.printf("Channel number: %d\n", channelNumber);
      // Serial.printf("Command type: %d\n", commandType);

      switch (channelNumber)
      {
      case TO_CONFIG_MODE_COMMAND_CODE:
        configModeInit();        
        return;
      case TEST_MODE_COMMAND_CODE:
        Serial.println("Test mode!");
        for (int i = 0; i < 6; i++)
        {
          outputStatusCodes[i] = BLINKING_CODE;
        }
        return; 
      case GLOBAL_REQUEST_COMMAND_CODE:
        int responseCode;
        if (leftChecked && rightChecked)
        {
          responseCode = OK_STATUS_CODE;
        }
        else
        {
          responseCode = READER_ERROR_STATUS_CODE;
        }
        String response = "";
        response += id;
        response += ":";
        response += responseCode;
        client.publish(publishRoot.c_str(), response.c_str());
        return;
      }

      switch (commandType)
      {
      case LIGHT_CODE:
        digitalWrite(getOutputCannel(channelNumber), HIGH);
        outputStatusCodes[channelNumber] = LIGHT_CODE;
        break;
      case BLINKING_CODE:
        digitalWrite(getOutputCannel(channelNumber), HIGH);
        outputStatusCodes[channelNumber] = BLINKING_CODE;
        break;
      case NOT_LIGHT_CODE:
        digitalWrite(getOutputCannel(channelNumber), LOW);
        outputStatusCodes[channelNumber] = NOT_LIGHT_CODE;
        break;
      default:
        Serial.println("Undefined command");
      }
      command = strtok_r(NULL, "_", &fullCommand);
    }
  }
}

void setup()
{
  delay(100);
  configurePins();
  //WiFi.softAPdisconnect(true);

  // Initialize SPIFFS
#ifdef ESP32
  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
#else
  if (!SPIFFS.begin())
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
#endif

  Serial.begin(115200);
  initConfigs();
  
  leftSemaphorHandler = xSemaphoreCreateBinary();
  rightSemaphorHandler = xSemaphoreCreateBinary();
  configSemaphorHandler = xSemaphoreCreateBinary();

  subscriptionRoot += commandsTopicRoot;
  subscriptionRoot += id;

  publishRoot += responsesTopicRoot;
  publishRoot += id;
  publishRoot += "/";

  xTaskCreatePinnedToCore(checkConfigModeTask, "checkConfigModeTask", 10000, NULL, 1, NULL, 1);  
  attachInterrupt(digitalPinToInterrupt(CONFIG_MODE_INPUT), checkConfigMode, FALLING);

  WiFi.begin(brokerSsid.c_str(), brokerPassword.c_str());
  while (WiFi.status() != WL_CONNECTED && configMode == false)
  {
    delay(200);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
  Serial.println("Connected to Wi-Fi");
  SPI.begin(); // Init SPI bus
  //SPI.setFrequency(1000000);
  pinMode(L_RST_PIN, OUTPUT);
  digitalWrite(L_RST_PIN, LOW);
  pinMode(R_RST_PIN, OUTPUT);
  digitalWrite(R_RST_PIN, LOW);
  delay(100);

  leftReader.PCD_Init();
  leftReader.PCD_SetAntennaGain(leftReader.RxGain_max);
  leftReader.PCD_WriteRegister(MFRC522::ComIEnReg, 0xA0); //Enable all interrupts
  
  delay(10);
  leftChecked = (leftReader.PCD_ReadRegister(leftReader.VersionReg) == 0x12);
  Serial.printf("Left reader checked: %s\n", leftChecked ? "TRUE" : "FALSE");

  rightReader.PCD_Init();
  rightReader.PCD_SetAntennaGain(rightReader.RxGain_max);
  rightReader.PCD_WriteRegister(MFRC522::ComIEnReg, 0xA0); //Enable all interrupts
  delay(10);
  rightChecked = (rightReader.PCD_ReadRegister(rightReader.VersionReg) == 0x12);
  Serial.printf("Right reader checked: %s\n", rightChecked ? "TRUE" : "FALSE");
  Serial.println();

  if (rightChecked && leftChecked) {
    digitalWrite(STATUS_LED_PIN, HIGH);
  }

  for (byte i = 0; i < 6; i++)
  {
    key.keyByte[i] = 0xFF;
  }

  client.setServer(brokerIp.c_str(), brokerPort); //connecting to mqtt server
  client.setCallback(callback);
 
  pinMode(R_IRQ_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(R_IRQ_PIN), checkRightReader, FALLING);

  pinMode(L_IRQ_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(L_IRQ_PIN), checkLeftReader, FALLING);

  leftReader.PICC_HaltA();
  rightReader.PICC_HaltA();
  delay(5);
  xTaskCreatePinnedToCore(leftTagReadersInterruptHandlingTask, "leftTagReadersInterruptHandlingTask", 10000, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(rightTagReadersInterruptHandlingTask, "rightTagReadersInterruptHandlingTask", 10000, NULL, 1, NULL, 1); 
  xTaskCreatePinnedToCore(blinkTask, "blinker", 1000, NULL, 1, NULL, 0);
  }
}

void loop()
{
  // put your main code here, to run repeatedly:
  if (configMode != true)
  {
    if ((!espClient.connected() || !client.connected()) && configMode == false)
    {
      reconnect();
    }
    client.loop();
  }
  else
  {
    digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
    delay(200);
    //printSPIFFSdata();
  }
}