#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#if defined(ESP8266)
#include <ESP8266WebServer.h>
#else
#include <WebServer.h>
#endif
#include <PubSubClient.h>

#define PDI_0 GPIO_NUM_39  //GPIO39 input only!
#define PDI_1 GPIO_NUM_36  //GPIO36 input only!
#define PDI_2 GPIO_NUM_34  //GPIO34 input only!
#define PDI_3 GPIO_NUM_35  //GPIO35 input only!
#define PDI_4 GPIO_NUM_32  //GPIO32
#define PDI_5 GPIO_NUM_33  //GPIO33
#define PDO_0 GPIO_NUM_25  //GPIO25
#define PDO_1 GPIO_NUM_26  //GPIO26
#define PDO_2 GPIO_NUM_27  //GPIO27
#define PDO_3 GPIO_NUM_14  //GPIO14
#define PDO_4 GPIO_NUM_12  //GPIO12
#define PDO_5 GPIO_NUM_13  //GPIO13

#define SO_0 GPIO_NUM_4 //GPIO4
#define SO_1 GPIO_NUM_16 //GPIO16
#define SO_2 GPIO_NUM_17 //GPIO17
#define SO_3 GPIO_NUM_5 //GPIO5
#define SO_4 GPIO_NUM_18 //GPIO18
#define SO_5 GPIO_NUM_19 //GPIO19
#define SO_6 GPIO_NUM_0 //GPIO0 schematic mistake!!!
#define SO_7 GPIO_NUM_21 //GPIO21
#define SO_8 GPIO_NUM_22 //GPIO22
#define SO_9 GPIO_NUM_23 //GPIO23

const int GLOBAL_REQUEST_COMMAND_CODE = 99;

const int NOT_LIGHT_CODE = 0;
const int LIGHT_CODE = 1;
const int BLINKING_CODE = 2;

volatile boolean inputStates[6];
int outputStatusCodes[10];

String id = "UC1";
String subscriptionRoot = "";
String publishRoot = "";

const char *ssid = "NATrain";
const char *password = "GoodLock";
const char *mqtt_server = "192.168.1.35";
const char *commandsTopicRoot = "NATrain/controlModules/commands/";
const char *responsesTopicRoot = "NATrain/controlModules/responses/";
const char *systemMessageRoot = "NATrain/system/";

WiFiClient espClient;
PubSubClient client(espClient); //lib required for mqtt

int LED = 02;

int getInputChannel(int chNumber)
{
  switch (chNumber)
  {
  case 0:
    return PDI_0;
  case 1:
    return PDI_1;
  case 2:
    return PDI_2;
  case 3:
    return PDI_3;
  case 4:
    return PDI_4;
  case 5:
    return PDI_5;
  default:
    return -1;
  }
}

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
  case 6:
    return SO_6;
  case 7:
    return SO_7;
  case 8:
    return SO_8;
  case 9:
    return SO_9;
  default:
    return -1;
  }
}

int getPDOutputChannel (int chNumber) {
  switch (chNumber) {
  case 0:
    return PDO_0;
  case 1:
    return PDO_1;
  case 2:
    return PDO_2;
  case 3:
    return PDO_3;
  case 4:
    return PDO_4;
  case 5:
    return PDO_5;
  default:
  return -1;
    break;
  
  }
}

void publishGlobalResponse () 
{
String response;
  for (int i = 0; i < 6; i++) {
    response += i;
    response += inputStates[i] ? ":1 " : ":0 ";
  }
  client.publish(publishRoot.c_str(), response.c_str());
} 

void callback(char* topic, byte* payload, unsigned int length) {   //callback includes topic and payload ( from which (topic) the payload is comming)
  String msgIN = "";
  for (int i = 0; i < length; i++)
  {
    msgIN += (char)payload[i];
  }
  Serial.print("Recieved message: ");
  Serial.println(msgIN);
  int channelNumber = 0;
  channelNumber += ((char)payload[0] - '0') * 10;
  channelNumber += ((char)payload[1] - '0') * 1;
  
  if (channelNumber == GLOBAL_REQUEST_COMMAND_CODE) {
    publishGlobalResponse(); //publish all inputs states right now;
    return;
  }
  int commandCode = (char)payload[3] - '0'; //casting to int
  switch (commandCode)
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
  default:
    client.publish(publishRoot.c_str(), ("wrong command: " + msgIN).c_str());
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.println("Attempting MQTT connection...");
    if (client.connect(id.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish(systemMessageRoot, (id + " connected").c_str());
      // ... and resubscribe
      client.subscribe(subscriptionRoot.c_str());
      Serial.print("subscribed to : ");
      Serial.println(subscriptionRoot);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void connectmqtt()
{
  client.connect(id.c_str());  // ESP will connect to mqtt broker with clientID
  {
    Serial.println("connected to MQTT");
    // Once connected, publish an announcement...

    // ... and resubscribe
      client.subscribe(subscriptionRoot.c_str());
      Serial.print("subscribed to : ");
      Serial.println(subscriptionRoot);
    if (!client.connected())
    {
      reconnect();
    }
  }
}

void blinkTask(void *pvParameters) { //blink signal lamps with blinking status codes every 1 second
  for(;;){
    for (int i = 0; i < 10; i++)
  {
    if (outputStatusCodes[i] == BLINKING_CODE)
    {
      int outputChannel = getOutputCannel(i);
      digitalWrite(outputChannel, !digitalRead(outputChannel));
    }
  }
    vTaskDelay(1000 / portTICK_RATE_MS);
  }
}

void checkInputsTask(void *pvParameters) {
  String response = "";
  for(;;){
  for (int i = 0; i < 6; i++)
  {
    digitalWrite(getPDOutputChannel(i), HIGH);
    vTaskDelay(500 / portTICK_RATE_MS);
    boolean occupationalState = digitalRead(getInputChannel(i));
    digitalWrite(getPDOutputChannel(i), LOW);
    if (inputStates[i] != occupationalState)
    {
      inputStates[i] = occupationalState;
      response += i;
      response += occupationalState ? ":1" : ":0"; 
      client.publish(publishRoot.c_str(), response.c_str());
      response = "";
    }
  }
  }
}

void configurePins() {
  pinMode(PDI_0, INPUT);
  pinMode(PDI_1, INPUT);
  pinMode(PDI_2, INPUT);
  pinMode(PDI_3, INPUT);
  pinMode(PDI_4, INPUT);
  pinMode(PDI_5, INPUT);
  pinMode(PDO_0, OUTPUT);
  pinMode(PDO_1, OUTPUT);
  pinMode(PDO_2, OUTPUT);
  pinMode(PDO_3, OUTPUT);
  pinMode(PDO_4, OUTPUT);
  pinMode(PDO_5, OUTPUT); 
  
  pinMode(SO_0, OUTPUT);
  pinMode(SO_1, OUTPUT);
  pinMode(SO_2, OUTPUT);
  pinMode(SO_3, OUTPUT);
  pinMode(SO_4, OUTPUT);
  pinMode(SO_5, OUTPUT);
  pinMode(SO_6, OUTPUT);
  pinMode(SO_7, OUTPUT);
  pinMode(SO_8, OUTPUT);
  pinMode(SO_9, OUTPUT);
}

void setup()
{
  configurePins();
 
  subscriptionRoot += commandsTopicRoot;
  subscriptionRoot += id;

  publishRoot += responsesTopicRoot;
  publishRoot += id;

  Serial.begin(9600);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  WiFi.begin(ssid, password);
  Serial.println("connected");
  client.setServer(mqtt_server, 1883);//connecting to mqtt server
  client.setCallback(callback);
  //delay(5000);
  connectmqtt();

  xTaskCreatePinnedToCore(blinkTask, "blinker", 1000, NULL, 1, NULL, 0);  
  xTaskCreatePinnedToCore(checkInputsTask, "inputChecker", 10000, NULL, 1, NULL, 0);  
  
}


void loop()
{
  // put your main code here, to run repeatedly:
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();
}


