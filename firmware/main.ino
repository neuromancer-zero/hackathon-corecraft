#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>
#include "LedController.hpp"
#include "ArduinoJson.h"
#include <WebSocketsClient_Generic.h>

char ssid[] = "";
char password[] = "";
char priceEndpoint[] = "/api/price";
char lastBlockEndpoint[] = "/api/block/tip";
String txId = "";
unsigned long ultimoTempo = 0;
const long intervalo = 1000;

WiFiClientSecure client;
WebSocketsClient webSocket;
ESP8266WebServer server(8080);

#define HOST "icesteel.com.br"
#define HOST_FINGERPRINT "9bffed94c74fd2c7651999c5f407dfc5d531bd4d6fba16e7e3832ee100d5e5b5"

#define LED_CONTROL_CLK_PIN D5
#define LED_CONTROL_DATA_PIN D6
#define LED_CONTROL_CS_PIN D7
#define LED_CONTROL_NUM_SEGMENTS 1

LedController ledController(LED_CONTROL_DATA_PIN, LED_CONTROL_CLK_PIN, LED_CONTROL_CS_PIN, LED_CONTROL_NUM_SEGMENTS);

void setup() {
  Serial.begin(115200);
 
  ledController.activateAllSegments();
  ledController.setIntensity(4);
  ledController.clearMatrix();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
 
  Serial.print("Connecting WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi Connected");
  Serial.println("IP Address: ");
  IPAddress ip = WiFi.localIP();
  Serial.println(ip);
  Serial.println(WiFi.macAddress());

  client.setInsecure();

  server.on("/", handleForm);
  server.on("/salvar", handleSalvar);
  server.begin();
  Serial.println("Webserver Iniciado.");
}

void handleForm() {
  Serial.println("200 Client Connected!");
  server.send(200, "text/html", R"(
      <html>
      <body>
          <h2>Configurar TX ID</h2>
          <form action='/salvar' method='GET'>
              <input type='text' name='id' placeholder='Digite o ID'>
              <button type='submit'>Salvar</button>
          </form>
      </body>
      </html>
  )");
}

void handleSalvar() {
  if (server.hasArg("id")) {
      txId = server.arg("id");
      Serial.println("ID salvo: " + txId);
      server.send(200, "text/html", R"(
      <html>
      <body>
          <h2>TX ID Salvo</h2>
          Aguardando a conclusao para exibir no dispositivo.
      </body>
      </html>
  )");
      conectarWebSocket(txId);
  }
}

void conectarWebSocket(String id) {
    String path = "/ws/tx/" + id;
    webSocket.begin(HOST, 80, path.c_str());
    webSocket.onEvent(onWebSocketEvent);
    webSocket.setReconnectInterval(5000);
}

bool conectado = false;

void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            conectado = true;
            Serial.println("Conectado!");
            break;

        case WStype_DISCONNECTED:
            if (conectado) {
                conectado = false;
                Serial.println("Desconectado!");
            }
            break;

        case WStype_TEXT: {
          writeReadyTX();
          StaticJsonDocument<256> doc;
          deserializeJson(doc, payload, length);

          String event = doc["event"].as<String>();

          if (event == "accepted") {
              Serial.println("TX aceita!");
          } else if (event == "mempool") {
              Serial.println("TX na mempool!");
          } else if (event == "confirmed") {
              int confs = doc["confirmations"];
              int block = doc["block"];
              Serial.printf("Confirmada! Confirmações: %d, Bloco: %d\n", confs, block);
          } else if (event == "done") {
              int confs = doc["confirmations"];
              Serial.printf("Concluído! Confirmações: %d\n", confs);
          } else if (event == "error") {
              String reason = doc["reason"].as<String>();
              Serial.println("Erro: " + reason);
          }
          break;
        }
    }
}

void writeBTCPrice() {
  ledController.clearMatrix();
  JsonDocument doc = makeHTTPRequest(priceEndpoint);
  String result = String(doc["usd"]);

  int len = result.length();
  for (int i = 0; i < len; i++) {
      int pos = len - 1 - i;
      ledController.setChar(0, pos, result[i], false);
  }
  delay(30000);
}

void writeLastBlockNum() {
  ledController.clearMatrix();
  JsonDocument doc = makeHTTPRequest(lastBlockEndpoint);
  String result = String(doc["height"]);

  int len = result.length();
  for (int i = 0; i < len; i++) {
      int pos = len - 1 - i;
      ledController.setChar(0, pos, result[i], false);
  }
  delay(30000);
}

void writeReadyTX() {
  ledController.clearMatrix();
  //READY
  //54321
  ledController.setChar(0, 4, 'r', false);
  ledController.setChar(0, 3, 'E', false);
  ledController.setChar(0, 2, 'A', false);
  ledController.setChar(0, 1, 'd', false);
  ledController.setChar(0, 0, 'Y', false);
  delay(30000);
}

JsonDocument makeHTTPRequest(char* endpoint) {
  client.setTimeout(10000);
  if (!client.connect(HOST, 443)) {
    Serial.println(F("Connection failed"));
    exit(0);
  }

  yield();

  client.print(F("GET "));
  client.print(endpoint);
  client.println(F(" HTTP/1.1"));

  client.print(F("Host: "));
  client.println(HOST);
  client.println(F("Cache-Control: no-cache"));

  if (client.println() == 0) {
    Serial.println(F("Failed to send request"));
    exit(0);
  }

  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    Serial.print(F("Unexpected response: "));
    Serial.println(status);
    exit(0);
  }

  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    Serial.println(F("Invalid response"));
    exit(0);
  }

  while(client.available() && client.peek() != '{') {
    char c = 0;
    client.readBytes(&c, 1);
    Serial.print(c);
    Serial.println("BAD");
  }

  StaticJsonDocument<512> doc;

  // Parse JSON object
  DeserializationError error = deserializeJson(doc, client);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    client.stop();
    exit(0);
  }
  
  // Disconnect
  client.stop();

  return doc;
}

void expertDelay() {
  if (millis() - ultimoTempo >= intervalo) {
    ultimoTempo = millis();
      Serial.println("tick");
  }
}

void loop() {
  webSocket.loop();
  server.handleClient();
  delay(30000);
  writeBTCPrice();
  writeLastBlockNum();
}