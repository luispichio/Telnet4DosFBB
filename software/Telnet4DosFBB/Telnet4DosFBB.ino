/*
MIT License

Copyright (c) 2017 Luis Pichio | https://sites.google.com/site/luispichio/ | https://github.com/luispichio

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "cbuf.h"
#include <EEPROM.h>
#include "CRC8.h"
#include "Utils.h"

//-----------------------------------------------------------------------------
//#define DEBUG_MODE
#define DEBUG_BAUD_RATE                             57600
//-----------------------------------------------------------------------------
#define SERIAL_BAUDRATE                             57600
#define DEFAULT_TELNET_PORT                         6300
#define WIFI_DISCONNECTED_RESET_TIMEOUT_MINUTES     5.0f  //m
#define WITHOUT_ACTIVITY_RESET_TIMEOUT_MINUTES      90.0f  //m
#define DEFAULT_CLIENT_INACTIVITY_TIMEOUT_SECONDS   60  //s
//-----------------------------------------------------------------------------
// ↓↓↓↓↓ parámetros internos ↓↓↓↓↓
//-----------------------------------------------------------------------------
#define SERIAL_RX_BUFFER_SIZE                       4096
#define SERIAL_RX_BUFFER_CTS_TARGET                 3072
//-----------------------------------------------------------------------------
#define TCP_2_SERIAL_BUFFER_SIZE                    8192
#define TCP_RX_FRAME_TIMEOUT                        50
#define SERIAL_2_TCP_BUFFER_SIZE                    1500  //1460?
#define SERIAL_RX_FRAME_TIMEOUT                     50
//-----------------------------------------------------------------------------
//pines
#define CTS_PIN                                     16
#define DCD_PIN                                     14
#define DSR_PIN                                     4
#define RTS_PIN                                     12
#define DTR_PIN                                     5
//-----------------------------------------------------------------------------
//estructura de configuración
//se almacena una única copia en memoria no volátil (flash en este caso)
typedef struct s_config{ 
  char wifiSSID[32];  //ssid de wifi al que se conecta el dispositivo
  char wifiPassword[32];  //contraseña del wifi
  uint16_t serverPort;  //puerto de servidor telnet
  uint16_t clientInactivityTimeout;  //tiempo de desconexión por unactividad (cliente)
  uint16_t duckDNSUpdateTime;  //tiempo de actualización de DNS (duckdns.org)
  char duckDNSDomain[64];  //dominio
  char duckDNSToken[64];  //token
  uint8_t crc; //control de integridad de la estructura
} t_config;
//-----------------------------------------------------------------------------
//variables globales
t_config g_config;  //configuración del sistema (se almacena también una copia en memoria no volátil)
WiFiServer *server;  //server telnet (falso telnet)
WiFiClient client;  //cliente entrante (conexión atentida del servidor) o saliente
long lastClientActivity;  //ultima actividad de cliente (utilizado para reinicio periódico)
bool online = true;  //sistema en línea
long lastWiFiStatusConnected;  //último estado "conectado" de WiFi
long lastDNSUpdate;

#ifdef DEBUG_MODE
  #include <SoftwareSerial.h>
  SoftwareSerial* logger;
  long gTCP2SerialByteCount = 0;
  long gSerial2TCPByteCount = 0;
#endif
//-----------------------------------------------------------------------------
long secondsToMilliseconds(float seconds){
  return seconds * 1000UL;
}
//-----------------------------------------------------------------------------
long minutesToMilliseconds(float minutes){
  return minutes * 60000UL;
}
//-----------------------------------------------------------------------------
long hoursToMilliseconds(float hours){
  return hours * 3600000UL;
}
//-----------------------------------------------------------------------------
//inicialización
void setup() {
  pinMode(DCD_PIN, OUTPUT);
  digitalWrite(DCD_PIN, HIGH);
  pinMode(CTS_PIN, OUTPUT);
  digitalWrite(CTS_PIN, HIGH);
  pinMode(DSR_PIN, OUTPUT);
//  digitalWrite(DSR_PIN, LOW);
  digitalWrite(DSR_PIN, HIGH);
  pinMode(RTS_PIN, INPUT);
  pinMode(DTR_PIN, INPUT);

//puerto serial  
  Serial.begin(SERIAL_BAUDRATE, SERIAL_8N1);
  Serial.setRxBufferSize(SERIAL_RX_BUFFER_SIZE);
  Serial.flush();
  Serial.swap();  //swap a gpio alternativo
  
#ifdef DEBUG_MODE
  logger = new SoftwareSerial(3, 1);
  logger->begin(DEBUG_BAUD_RATE);
  logger->println("\n\nDEBUG_MODE");
#endif

//configuración (almacenada en memoria no volátil)
  EEPROM.begin(512);
  EEPROM.get(0, g_config);
  if (CRC8.calc(0xFF, (uint8_t *)&g_config, offsetof(t_config, crc)) != g_config.crc){  //si la copia tiene integridad incorrecta -> configuración por defecto
#ifdef DEBUG_MODE  
      logger->println("Factory defaults!");
#endif    
      strcpy(g_config.wifiSSID, "");
      strcpy(g_config.wifiPassword, "");
      g_config.serverPort = DEFAULT_TELNET_PORT;
      g_config.clientInactivityTimeout = DEFAULT_CLIENT_INACTIVITY_TIMEOUT_SECONDS;
      g_config.duckDNSUpdateTime = 0;  //DNS desactivado por defecto
      strcpy(g_config.duckDNSDomain, "");
      strcpy(g_config.duckDNSToken, "");
      g_config.crc = CRC8.calc(0xFF, (uint8_t *)&g_config, offsetof(t_config, crc));
      EEPROM.put(0, g_config);
      EEPROM.commit();
  }
   
//wifi
  if (strlen(g_config.wifiSSID))
    WiFi.begin(g_config.wifiSSID, g_config.wifiPassword);  //intentamos conectar
  else
    WiFi.begin();
#ifdef DEBUG_MODE  
  logger->print("Conectando a "); logger->println(g_config.wifiSSID);
#endif  
  while ((WiFi.status() != WL_CONNECTED) && (millis() < secondsToMilliseconds(10.0f)))  //por 10s
    delay(1000);
#ifdef DEBUG_MODE    
  if (WiFi.status() == WL_CONNECTED){
    logger->print("Listo! Para conectar utilice 'telnet ");
    logger->print(WiFi.localIP());
    logger->print(":");
    logger->print(g_config.serverPort);
    logger->println("'");
  } else {
    logger->print("No se pudo conectar a ");
    logger->println(g_config.wifiSSID);
  }
#endif

//servidor telnet (falso telnet en rigor)
  server = new WiFiServer(g_config.serverPort);
  server->begin();
  server->setNoDelay(false);

  lastClientActivity = lastWiFiStatusConnected = lastDNSUpdate = millis();
}
//-----------------------------------------------------------------------------
bool clientConnected(){
  return client && client.connected();
}
//-----------------------------------------------------------------------------
//procesa trama de comandos
void processFBBLine(char *frame){
  char *cmd;
  char *host;
  char *_port;
  int port;
  bool _connected;
  byte retrys = 3;
#ifdef DEBUG_MODE  
  logger->println(frame);
#endif

//comando conectar
  if (cmd = strstr(frame, "C$")){
    cmd += 2;
    host = strtok(cmd, ":");
    _port = strtok(NULL, ":");
    port = atoi(_port);
    if (port == 0)
      port = 23;
    if (host){
      while (!(_connected = client.connect(host, port)) && retrys){
        retrys--;
        delay(100);
      }
      if (_connected){
//        Serial.print("Conectando a "); Serial.print(host); Serial.print(":"); Serial.println(port);
        client.setNoDelay(false);
        digitalWrite(DCD_PIN, LOW);
        lastClientActivity = millis();
      } else {
//        Serial.print("Imposible conectar a "); Serial.print(host); Serial.print(":"); Serial.println(port);
        digitalWrite(DCD_PIN, LOW);
        delay(100);
        digitalWrite(DCD_PIN, HIGH);
      }
    }
  }

//comando SSID de wifi
  if (cmd = strstr(frame, "SSID$")){
    cmd += strlen("SSID$");
    if (strlen(cmd) < 31)
      strcpy(g_config.wifiSSID, cmd);
  }

//comando password de wifi
  if (cmd = strstr(frame, "PASS$")){
    cmd += strlen("PASS$");
    if (strlen(cmd) < 31)
      strcpy(g_config.wifiPassword, cmd);
  }
  
//puerto de servidor telnet
  if (cmd = strstr(frame, "PORT$")){
    cmd += strlen("PORT$");
    g_config.serverPort = atoi(cmd);
  }

//DDNS
  if (cmd = strstr(frame, "DUCK_DNS_UPDATE_TIME$")){
    cmd += strlen("DUCK_DNS_UPDATE_TIME$");
    g_config.duckDNSUpdateTime = atoi(cmd);
  }
  if (cmd = strstr(frame, "DUCK_DNS_DOMAIN$")){
    cmd += strlen("DUCK_DNS_DOMAIN$");
    if (strlen(cmd) < 63)
      strcpy(g_config.duckDNSDomain, cmd);
  }
  if (cmd = strstr(frame, "DUCK_DNS_TOKEN$")){
    cmd += strlen("DUCK_DNS_TOKEN$");
    if (strlen(cmd) < 63)
      strcpy(g_config.duckDNSToken, cmd);
  }
  
//almacenamos configuración en caso de que haya cambios
  uint8_t crc = CRC8.calc(0xFF, (uint8_t *)&g_config, offsetof(t_config, crc));
  if (crc != g_config.crc){
    g_config.crc = crc;
    EEPROM.put(0, g_config);
    EEPROM.commit();
  }

//bbs en línea
  if (strstr(frame, "ONLINE$"))
    online = true;

//bbs fuera de línea
  if (strstr(frame, "OFFLINE$"))
    online = false;

//reinicio (para aplicar la configuración)  
  if (strstr(frame, "RESET$")){
    Serial.swap();
    ESP.restart();
  }
}
//-----------------------------------------------------------------------------
//procesa trama de puerto serial
void processSerialFrame(uint8_t *frame, int size){
  static char bufferRX[256];
  static int bufferSize = 0;  
  while (size--){
    uint8_t b = *frame; frame++;
    if ((b == 0x0a) || (b == 0x0d)){
      if (bufferSize){
        bufferRX[bufferSize] = 0;
        processFBBLine(bufferRX);
        bufferSize = 0;
      }
    } else
      if (bufferSize < 255)  //si mayor a tamaño de buffer -> se pierde contenido
        bufferRX[bufferSize++] = b;
  }
}
//-----------------------------------------------------------------------------
//manejador de conexiones entrantes (servidor telnet)
//atiende nuevos clientes
//envía mensaje y rechaza en caso de no poder atender la conexión
bool check4Clients(void){
  if (server->hasClient()){  //hay nuevo cliente?
    if (online){  //está el servicio en línea?
      if (clientConnected()){  //tenemos cliente -> no podemos atender al nuevo
        WiFiClient _client = server->available();
        _client.println("Todos los canales están ocupados.");  //mensaje de "canales ocupados"
        _client.println("Intente en otro momento.");
        delay(0);
        _client.stop();
      } else {  //no tenemos cliente, tomamos la conexión
        if (client)  //tenemos cliente (pero no está conectado)
          client.stop();
        client = server->available();
        client.setNoDelay(false);
        lastClientActivity = millis();
        client.print("\xff\xfc\x01\xff\xfe\x01");  //IAC WONT ECHO & IAC DONT ECHO
        delay(0);
        digitalWrite(DCD_PIN, LOW);  //señal de DCD para que el BBS "atienda la conexión"
//        client.flush();
      }
    } else {  //el servicio no está en línea
        WiFiClient _client = server->available();
        _client.println("BBS fuera de línea");  //mensaje de "fuera de línea"
        _client.println("Intente en otro momento.");
        delay(0);
        _client.stop();      
    }
  }  
  return clientConnected();

}
//-----------------------------------------------------------------------------
//recepción de tramas por tcp y reenvío a puerto serie
void tcp2Serial(){
  static uint8_t buffer[TCP_2_SERIAL_BUFFER_SIZE];  //estático para evitar uso de stack (fragmentación? no debería)
  uint16_t bufferLast = 0;
  uint16_t bufferFirst = 0;
  uint16_t lastAvailable;
  long lastRX;
  uint16_t available;  

  if (available = client.available()){
    lastRX = millis();
    lastAvailable = available;
    do {
      if (bufferFirst < bufferLast){  //hay contenido para el puerto serie
        if (available = Utils::Min(bufferLast - bufferFirst, Serial.availableForWrite())){  //hay contenido / espacio en FIFO de puerto serie?
          Serial.write(buffer + bufferFirst, available);  //enviamos por puerto serial (al FIFO)
          bufferFirst += available;
        }
      }
      if (bufferFirst >= bufferLast){  //puerto serie sin trabajo? (quiere decir que podemos atender lo que llega por TCP)
        bufferLast = Utils::Min(client.available(), TCP_2_SERIAL_BUFFER_SIZE);
        client.read(buffer, bufferLast);
        bufferFirst = 0;
#ifdef DEBUG_MODE        
        gTCP2SerialByteCount += bufferLast;
#endif        
      }
      if ((available = client.available()) != lastAvailable){  //hay RX?
          lastAvailable = available;
          lastRX = millis();          
      }    
      delay(0);
      digitalWrite(CTS_PIN, Serial.available() > SERIAL_RX_BUFFER_CTS_TARGET);  //si se alcanza cierto contenido en buffer de rx de puerto serie -> cerramos la ventana de RX del FBB  
    } while (available || (bufferFirst < bufferLast) || (millis() - lastRX < TCP_RX_FRAME_TIMEOUT));
    lastClientActivity = millis();
  }      
}
//-----------------------------------------------------------------------------
//recepción de tramas por puerto serie y reenvío a cliente TCP
void serial2TCP(bool _clientConnected){
  static uint8_t buffer[SERIAL_2_TCP_BUFFER_SIZE];  //estático para evitar uso de stack (fragmentación? no debería)
  uint16_t lastAvailable;
  long lastRX;
  uint16_t available;      

  if (available = Serial.available()){
    lastRX = millis();
    lastAvailable = available;
    do {
      if (_clientConnected){  //si hay cliente conectado -> enviamos datos al cliente
        if (available = Utils::Min(available, SERIAL_2_TCP_BUFFER_SIZE, client.availableForWrite())){  //si hay datos disponibles en buffer de RX de Serial => limitamos a tamaño de buffer  
          Serial.readBytes(buffer, available);  //leemos los bytes
          client.write((const uint8_t *)buffer, available);  //enviamos trama al cliente
          lastClientActivity = millis();
  #ifdef DEBUG_MODE         
          gSerial2TCPByteCount += available;
  #endif
        }
      } else {  //sinó, enviamos a procesador de comandos
        available = Utils::Min(Serial.available(), SERIAL_2_TCP_BUFFER_SIZE);  //limitamos a tamaño de buffer
        Serial.readBytes(buffer, available);  //leemos los bytes
        processSerialFrame(buffer, available);  //procesamos como trama de comandos
      }
      
      if ((available = Serial.available()) != lastAvailable){  //hay RX?
          lastAvailable = available;
          lastRX = millis();          
      }    
      delay(0);
      digitalWrite(CTS_PIN, Serial.available() > SERIAL_RX_BUFFER_CTS_TARGET);  //si se alcanza cierto contenido en buffer de rx de puerto serie -> cerramos la ventana de RX del FBB  
    } while (available || (millis() - lastRX < SERIAL_RX_FRAME_TIMEOUT));
  }
}
//-----------------------------------------------------------------------------
//actualización de DNS (duckdns.org)
bool doDNSUpdate(){
  char str[256];
  HTTPClient http;
  sprintf(str, "http://www.duckdns.org/update?domains=%s&token=%s", g_config.duckDNSDomain, g_config.duckDNSToken);
#ifdef DEBUG_MODE   
  logger->println(str);
#endif
  http.begin(str);
//  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  if (http.GET() == 200){
    http.getString().toCharArray(str, 256);
#ifdef DEBUG_MODE    
    logger->println(str);
#endif
    if (strstr(str, "OK"))
      return true;
  }
  return false;
}
//-----------------------------------------------------------------------------
//loop de la aplicación
long oneSecond = millis();
void loop() {
  bool _clientConnected = check4Clients();
  digitalWrite(DCD_PIN, !_clientConnected);  //cliente conectado -> señal de atender al FBB
  digitalWrite(CTS_PIN, Serial.available() > SERIAL_RX_BUFFER_CTS_TARGET);  //si se alcanza cierto contenido en buffer de rx de puerto serie -> cerramos la ventana de RX del FBB  
  
  if (client) tcp2Serial();
  serial2TCP(_clientConnected);
    
  //chequeos periódicos
  if (millis() - oneSecond > 1000){
#ifdef DEBUG_MODE 
    char str[128];
    long delta = millis() - oneSecond;
    sprintf(str, "%li %li %li %li %li", 1000 * gTCP2SerialByteCount / delta, 1000 * gSerial2TCPByteCount / delta, millis() - lastClientActivity, ESP.getFreeHeap(), ESP.getHeapFragmentation());
    gSerial2TCPByteCount = gTCP2SerialByteCount = 0;
    logger->println(str);
#endif
    oneSecond = millis();
    //desconexión por inactividad / fuera de línea
    if (_clientConnected && (millis() - lastClientActivity > secondsToMilliseconds(g_config.clientInactivityTimeout) || !online))
      client.stop();
    //actualización de DNS
    if ((!_clientConnected) && g_config.duckDNSUpdateTime && (millis() - lastDNSUpdate > secondsToMilliseconds(g_config.duckDNSUpdateTime))){
      doDNSUpdate();
      lastDNSUpdate = millis();
    }
    //reinicio por inactividad / falta de WiFi (por cuestiones de estabilidad de la plataforma)
    if (millis() - lastClientActivity > minutesToMilliseconds(WITHOUT_ACTIVITY_RESET_TIMEOUT_MINUTES))
      ESP.restart();  
    //chequeo de WiFi
//    Serial.print(WiFi.status());
    if (WiFi.status() == WL_CONNECTED)
      lastWiFiStatusConnected = millis();
    if (millis() - lastWiFiStatusConnected > minutesToMilliseconds(WIFI_DISCONNECTED_RESET_TIMEOUT_MINUTES))
      ESP.restart();
  }
}
//-----------------------------------------------------------------------------


