//2021.01.12 - 
// upload to github 2022.02.09
//Alexaと連携
//ドアの施錠・解錠を行う

#include <Servo.h>

#include <Espalexa.h>
 #ifdef ARDUINO_ARCH_ESP32
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h> //if you get an error here please update to ESP32 arduino core 1.0.0
#else
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#endif

//チャタリング防止のためのタイマ割り込み用
#include <Ticker.h>

// prototypes
boolean connectWifi();

void onTimer();

void lockDoor();
void openDoor();
void switchDoor();

String getHttp(const char* host);
void printHtml();

//callback functions
void controlDoor(EspalexaDevice* d);

// Change this!!
const char* ssid = "hogehoge";
const char* password = "hogehoge";
IPAddress ip(hogehoge);         // for fixed IP Address
IPAddress subnet(hogehoge);     //

//SwitherIPアドレス, URL
const char* switcher = "http://hoge";
const char* switcher_switch12_on = "http://hoge";
const char* switcher_switch12_off = "http://hoge";

boolean wifiConnected = false;

Espalexa espalexa;
#ifdef ARDUINO_ARCH_ESP32
WebServer server(80);
#else
ESP8266WebServer server(80);
#endif


//サーボモータ
Servo servo;

const int SERVO_PIN = 25;
const int LOCKED_POS = 90;
const int OPEN_POS = LOCKED_POS - 90;
const int WAIT_TIME = 550; //回転待ち時間

//解錠LED
const int LED_OPEN_PIN = 2;

//スイッチ
const int SW_PIN = 15; //15番ピンはデフォルトでPULLUP(ノイズ防止)
int chat_count = 0;    //チャタリング防止用
Ticker timer, openTimer, lockTimer;

//ドアの状態を保持
String doorState = "locked";
//外出状態の保持
boolean outing = false;

//帰宅時の自動解錠待ち時間[s]
const int OPEN_TIME = 30;
//帰宅時の自動施錠待ち時間[s]
const int LOCK_TIME = 300;


void setup(){
  Serial.begin(115200);

  //LEDセットアップ
  pinMode(LED_OPEN_PIN, OUTPUT);

  //ボタンセットアップ
  pinMode(SW_PIN, INPUT_PULLUP);

  //ボタン割り込み処理
  timer.attach_ms(1, onTimer);
  
  lockDoor(); //ドアの鍵を閉める
  
  // Initialise wifi connection
  wifiConnected = connectWifi();

  //ここにブラウザでの処理を書く
  if(wifiConnected){
    server.on("/", HTTP_GET, [](){
      //現在の状態を表示するのみ
      printHtml();
    });

    server.on("/open", HTTP_GET, [](){

      openDoor();
      printHtml();
    });

    server.on("/locked", HTTP_GET, [](){

      lockDoor();
      printHtml();
    });
    
    server.on("/switch", HTTP_GET, [](){

      switchDoor();
      printHtml();
    });

    //外出時の動作
    server.on("/out/switch", HTTP_GET, [](){
      switchDoor();
      printHtml();

      openTimer.detach();
      lockTimer.detach();

      //外出フラグの設定
      if( doorState == "locked" ){
        outing = true;
      }else{
        outing = false;
      }
    });

    //帰宅時の自動タイマー解錠
    server.on("/out/open", HTTP_GET, [](){
      printHtml();
      
      if(outing){
        openTimer.once(OPEN_TIME, openDoor);
        lockTimer.once((OPEN_TIME + LOCK_TIME), lockDoor);
      }
    });
    
    server.onNotFound([](){
      if (!espalexa.handleAlexaApiCall(server.uri(),server.arg(0))) //if you don't know the URI, ask espalexa whether it is an Alexa control request
      {
        //whatever you want to do with 404s
        server.send(404, "text/plain", "Not found");
      }
    });

    // Define your devices here.
    espalexa.addDevice("Door Key", controlDoor); //simplest definition, default state off

    espalexa.begin(&server); //give espalexa a pointer to your server object so it can use your server instead of creating its own
    //server.begin(); //omit this since it will be done by espalexa.begin(&server)
  }else{
    while(1){
      Serial.println("Cannot connect to WiFi. Please check data and reset the ESP.");
      delay(2500);
    }
  }
}
 
void loop(){
   //server.handleClient() //you can omit this line from your code since it will be called in espalexa.loop()
   espalexa.loop();
   delay(1);
}

//ドア鍵制御
void lockDoor(){

  digitalWrite(LED_OPEN_PIN, LOW);

  servo.attach(SERVO_PIN);
  servo.write(LOCKED_POS);
  delay(WAIT_TIME);
  servo.detach();
  
  doorState = "locked";
  Serial.println("Door is locked.");
}

void openDoor(){

  digitalWrite(LED_OPEN_PIN, HIGH);

  servo.attach(SERVO_PIN);
  servo.write(OPEN_POS);
  delay(WAIT_TIME);
  servo.detach();
  
  doorState = "open";
  Serial.println("Door is open.");
}

void switchDoor(){
  // doorStateがlockedなら解錠、それ以外なら施錠
  if (doorState=="locked") {
    openDoor();
  } else {
    lockDoor();
  } 
}


//HTTP通信
String getHttp(const char* host){
  HTTPClient http;

  http.begin(host);
  int httpCode = http.GET();

  String result = "";

  if (httpCode < 0) {
    result = http.errorToString(httpCode);
  } else if (http.getSize() < 0) {
    result =  "size is invalid";
  } else {
    result = http.getString();
  }

  http.end();
  return result;
}

void onTimer(){
  if( digitalRead(SW_PIN) == LOW ){
    chat_count++;

    //20ms LOWだったらドアの解錠/施錠
    if( chat_count == 20 ){
      switchDoor();

      //(内側から)ドアを解錠したのなら、部屋の電気(switchに接続)を消す
      if( doorState == "open" ){
        getHttp(switcher_switch12_off);
        
      }else{  //(内側から)ドアを施錠したのなら、部屋の電気(switchに接続)を消す
        getHttp(switcher_switch12_on);

        //外出フラグを下す
        outing = false;
      }
    }
  }else{
    chat_count = 0;
  }
}

// HTML
void printHtml(){
  
  char temp[1000];  //HTML文格納用
  char button[100];
  char outingState[20];

  // doorStateがlockedなら、openにするボタンを表示
  if (doorState=="locked") {
    snprintf(button, 100, "<p><a href=\"/open\"><button class=\"button\">OPEN</button></a></p>");
  } else {
    snprintf(button, 100, "<p><a href=\"/locked\"><button class=\"button button2\">LOCK</button></a></p>");
  } 

  //外出中かどうか
  if(outing){
    snprintf(outingState, 20, "outing");
  }else{
    snprintf(outingState, 20, "staying home");
  }
  
  //HTML文の代入 (HTMLの開始とOPEN/LOCKボタンのCSS, HTMLの終了)
  snprintf(temp, 1000,
         "<!DOCTYPE html><html>\
            <head>\
              <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
              <link rel=\"icon\" href=\"data:,\">\
              <style>\
                html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\
                .button { background-color: #4169E1; border: none; color: white; padding: 16px 40px;\
                text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}\
                .button2 {background-color: #555555;}\
              </style>\
            </head>\
            <body>\
              <h1>DoorLocker</h1>\
              <p>State: %s</p>\
              %s\
              <p>OutingState: %s</p>\
            </body>\
          </html>", doorState, button, outingState);
  
  server.send(200, "text/html", temp);
}

//our callback functions
void controlDoor(EspalexaDevice* d){
  if (d == nullptr) return; //this is good practice, but not required

  if (d->getValue()){
    openDoor(); //解錠
  }else {
    lockDoor(); //施錠
  }
}

// connect to wifi – returns true if successful or false if not
boolean connectWifi(){
  boolean state = true;
  int i = 0;

  // Set fixed IP address
  if (!WiFi.config(ip,ip,subnet)){
     Serial.println("Failed to configure!");
 }
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");
  Serial.println("Connecting to WiFi");

  // Wait for connection
  Serial.print("Connecting...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (i > 20){
      state = false; break;
    }
    i++;
  }
  Serial.println("");
  if (state){
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
  else {
    Serial.println("Connection failed.");
  }
  delay(100);
  return state;
}
