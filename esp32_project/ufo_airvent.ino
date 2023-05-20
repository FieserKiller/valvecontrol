/*
 * Listens for POST /open, POST /close and POST /status http calls, opens 
 * or closes a valve and starts/stops the fan accordingly.
 * Changelog:
 * - disables servo after command to make it live longer
 * - reconnects wifi if disconnected
 * - reconnects wifi if it looks connected but gateway ping does not respond
 * - lights LED while pinging gateway and switches it off when gateway was found successfully
 * - triggers a relay before every servo movement and shuts it down afterwards to prevent jerk
 * - triggers fan relay on and off. on close fan relay is switched off and we wait 30sek for it to pin down before valve is closed
 * - /status call was added which returns OPEN, CLOSED, UNDEF
 * - fast blinking added while waiting for fan spin down
 * - hardware watch dog added to reset device when gateway ping fails for 5 minutes
 * - added restart command to reset esp32
 */
#include <WiFi.h>
#include <ESP32Servo.h>
#include <ESPping.h>
#include <esp_task_wdt.h>

// Watchdog countdown timer in seconds
#define WDT_TIMEOUT 300 


// wiFi settings 
const char* ssid     = "wifi_name";
const char* password = "wifi_password";
const char* HOSTNAME = "device_name";

// Pin config
int servoPin = 13;
int valveRelayPin = 14;
int fanRelayPin = 27;

// Servo settings
int minAngle = 0;
int maxAngle = 180;
int servoMin = 850;
int servoMax = 2250;



// heap vars
unsigned long previousMillis = 0;
unsigned long wifiCheckInterval = 60000;
bool wasOk = false;
enum Status {OPEN, CLOSED, UNDEF} status = UNDEF;
enum Info {STATE, HELP, NONE} info = NONE;
bool ranOnce = false;
Servo myservo;
WiFiServer server(80);

void setup()
{
  
  Serial.begin(115200);
  delay(10);

  esp_task_wdt_init(WDT_TIMEOUT, true); //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL); //add current thread to WDT watch

  // Wifi setup
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  Serial.print("Hostname:");
  Serial.println(HOSTNAME);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  server.begin();

  // Servo setup
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  myservo.setPeriodHertz(50);

  //pin setup
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(valveRelayPin, OUTPUT);
  pinMode(fanRelayPin, OUTPUT);
  digitalWrite(fanRelayPin, LOW);
  digitalWrite(valveRelayPin, LOW);
}

void loop() {
  wasOk = false;
  info = NONE;
  WiFiClient client = server.available();   // listen for incoming clients
  if (client) {                             // if you get a client,
    Serial.println("New Client.");           // print a message out the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        //Serial.write(c);                    // print it out the serial monitor
        if (c == '\n') {                    // if the byte is a newline character

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so break out of the while loop:
          if (currentLine.length() == 0) {
            break;
          } else {    // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        //check for commands in call
        if (currentLine.endsWith("POST /open")) {
          Serial.println(currentLine + "\nDoing OPEN");
          doOpen();
          wasOk = true;
        } else if (currentLine.endsWith("POST /close")) {
          Serial.println(currentLine + "\nDoing CLOSE");
          doClose();
          wasOk = true;
        } else if (currentLine.endsWith("POST /status")) {
          Serial.println(currentLine + "\nDoing STATUS");
          info = STATE;
          wasOk = true;
        } else if (currentLine.endsWith("POST /restart")) {
          Serial.println(currentLine + "\nDoing RESTART");
          doReset();
          wasOk = true;
        } 
      }
    }
    if (!wasOk) {
      printERROR(client);
    } else {
      printOK(client);
    }

    // close the connection:
    client.stop();
    Serial.println("Client Disconnected.");
  }


  // if WiFi is down, try reconnecting
  unsigned long currentMillis = millis();
  if (!ranOnce || currentMillis - previousMillis >= wifiCheckInterval) {
    if (!heartbeatPing() || (WiFi.status() != WL_CONNECTED)) {
      Serial.println("Reconnecting to WiFi...");
      WiFi.disconnect();
      WiFi.reconnect();
      heartbeatPing();
    } else{
      previousMillis = currentMillis;
    }
    ranOnce = true;
  }

}

void printOK(WiFiClient client) {
  client.println("HTTP/1.1 200 OK");
  printResponse(client);
}

void printERROR(WiFiClient client) {
  client.println("HTTP/1.1 404 NOT FOUND");
  info = HELP;
  printResponse(client);
}

void printResponse(WiFiClient client){
  if(info == STATE || info == HELP) {
    client.println("Content-Type: application/json");
  }
  client.println("Connection: close");
  client.println();
  if(info == STATE){
    String statusString;
    switch(status)
    {
    case OPEN: 
      statusString = "OPEN"; 
      break;
    case CLOSED: 
      statusString = "CLOSED"; 
      break;
    default: 
      statusString = "UNDEF"; 
      break;  
    }
    client.println("{\"status\":\""+statusString+"\"}");
  } else if(info == HELP){
    client.println("{\"commands\":\"POST open, close, status, restart\"}");
  }
}


void doClose() {
  status = CLOSED;
  digitalWrite(fanRelayPin, LOW);
  delayBlinking(30000);
  myservo.attach(servoPin, servoMin, servoMax);
  myservo.write(minAngle);
  digitalWrite(valveRelayPin, HIGH);
  delay(2000);
  digitalWrite(valveRelayPin, LOW);
  delay(100);
  myservo.detach();
}

void doOpen() {
  status = OPEN;
  myservo.attach(servoPin, servoMin, servoMax);
  myservo.write(maxAngle);
  digitalWrite(valveRelayPin, HIGH);
  delay(2000);
  digitalWrite(valveRelayPin, LOW);
  delay(100);
  myservo.detach();
  digitalWrite(fanRelayPin, HIGH);
}

void delayBlinking(int delayMillis){
  int step = 250;
  int steps = delayMillis / step;
  for(int i = 0; i<=steps;i++){
    digitalWrite(LED_BUILTIN, HIGH);
    delay(step/2);
    digitalWrite(LED_BUILTIN, LOW);
    delay(step/2);
  }
}

bool heartbeatPing() {
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("Heartbeat gateway ping...");
  if (Ping.ping(WiFi.gatewayIP(),1)) {
    Serial.println("We are fine");
    digitalWrite(LED_BUILTIN, LOW);
    esp_task_wdt_reset(); //reset watchdog timer
    return true;
  }  
  Serial.println("We are offline");
  return false;
}

void doReset(){
  Serial.print("Restarting...");
  ESP.restart();
}
