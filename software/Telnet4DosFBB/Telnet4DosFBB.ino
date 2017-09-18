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

#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include "cbuf.h"
#include <EEPROM.h>
#include "CRC8.h"
#include "Utils.h"

//pines
#define CTS_PIN                             4
#define DCD_PIN                             14

//estructura de configuración
//se almacena una única copia en memoria no volátil (flash en este caso)
typedef struct s_config{ 
  char wifiSSID[32];  //ssid de wifi al que se conecta el dispositivo
  char wifiPassword[32];  //contraseña del wifi
  uint16_t serverPort;  //puerto de servidor telnet
  uint16_t clientInactivityTimeout;  //tiempo de desconexión por unactividad (cliente)
  uint8_t crc; //control de integridad de la estructura
} t_config;

//variables globales
t_config g_config;  //configuración del sistema (se almacena también una copia en memoria no volátil)
WiFiServer *server;  //server telnet (falso telnet)
WiFiClient client;  //cliente entrante (conexión atentida del servidor) o saliente
long lastClientActivity;  //ultima actividad de cliente (utilizado para reinicio periódico)
cbuf telnetBuffer(16384);  //buffer de tramas recibidas por el cliente (es mucho mas veloz que el puerto serial)
cbuf serialBuffer(2048);  //buffer intermedio de puesto serial (el buffer de rx del Serial (hardware) es de 128bytes)
bool online = true;  //sistema en línea

//inicialización
void setup() {
  pinMode(DCD_PIN, OUTPUT);
  digitalWrite(DCD_PIN, HIGH);
  pinMode(CTS_PIN, OUTPUT);
  digitalWrite(CTS_PIN, LOW);

//configuración (almacenada en memoria no volátil)
  EEPROM.begin(512);
  EEPROM.get(0, g_config);
  if (CRC8.calc(0xFF, (uint8_t *)&g_config, offsetof(t_config, crc)) != g_config.crc){  //si la copia tiene integridad incorrecta -> configuración por defecto
      strcpy(g_config.wifiSSID, "");
      strcpy(g_config.wifiPassword, "");
      g_config.serverPort = 6300;
      g_config.clientInactivityTimeout = 60;      
      g_config.crc = CRC8.calc(0xFF, (uint8_t *)&g_config, offsetof(t_config, crc));
      EEPROM.put(0, g_config);
      EEPROM.commit();
  }

//wifi
  Serial.begin(57600, SERIAL_8N1);
  WiFi.begin(g_config.wifiSSID, g_config.wifiPassword);  //intentamos conectar
  Serial.print("\nConectando a "); Serial.println(g_config.wifiSSID);
  while ((WiFi.status() != WL_CONNECTED) && (millis() < 20000))  //por 20s
    delay(1000);
  if (WiFi.status() == WL_CONNECTED){
    Serial.print("Listo! Para conectar utilice 'telnet "); Serial.print(WiFi.localIP()); Serial.print(":"); Serial.print(g_config.serverPort); Serial.println("'");
  } else {
    Serial.print("No se pudo conectar a "); Serial.println(g_config.wifiSSID);
  }

//puerto serial  
  Serial.flush();
  Serial.swap();  //swap a gpio alternativo

//servidor telnet (falso telnet en rigor)
  server = new WiFiServer(g_config.serverPort);
  server->begin();
  server->setNoDelay(true);

  lastClientActivity = millis();
}

bool clientConnected(){
  return client && client.connected();
}

//procesa trama de comandos
void processFBBLine(char *frame){
  char *cmd;
  char *host;
  char *_port;
  int port;

//comando conectar
  if ((cmd = strstr(frame, "C$")) || (cmd = strstr(frame, "c$"))){
    cmd += 2;
    host = strtok(cmd, ":");
    _port = strtok(NULL, ":");
    port = atoi(_port);
    if (port == 0)
      port = 23;
    if (host){
      if (client.connect(host, port)){
//        Serial.print("Conectando a "); Serial.print(host); Serial.print(":"); Serial.println(port);
        client.setNoDelay(false);
        digitalWrite(DCD_PIN, LOW);
        lastClientActivity = millis();
        delay(50);
      } else {
//        Serial.print("Imposible conectar a "); Serial.print(host); Serial.print(":"); Serial.println(port);
        digitalWrite(DCD_PIN, LOW);
        delay(50);
        digitalWrite(DCD_PIN, HIGH);
      }
    }
  }

//comando SSID de wifi
  if ((cmd = strstr(frame, "SSID$")) || (cmd = strstr(frame, "ssid$"))){
    cmd += 5;
    if (strlen(cmd) < 31)
      strcpy(g_config.wifiSSID, cmd);
  }

//comando password de wifi
  if ((cmd = strstr(frame, "PASS$")) || (cmd = strstr(frame, "pass$"))){
    cmd += 5;
    if (strlen(cmd) < 31)
      strcpy(g_config.wifiPassword, cmd);
  }
  
//puerto de servidor telnet
  if ((cmd = strstr(frame, "PORT$")) || (cmd = strstr(frame, "port$"))){
    cmd += 5;
    g_config.serverPort = atoi(cmd);
  }

//almacenamos configuración en caso de que haya cambios
  uint8_t crc = CRC8.calc(0xFF, (uint8_t *)&g_config, offsetof(t_config, crc));
  if (crc != g_config.crc){
    g_config.crc = crc;
    EEPROM.put(0, g_config);
    EEPROM.commit();
  }

//bbs en línea
  if (strstr(frame, "ONLINE$") || strstr(frame, "online$"))
    online = true;

//bbs fuera de línea
  if (strstr(frame, "OFFLINE$") || strstr(frame, "offline$"))
    online = false;

//reinicio (para aplicar la configuración)  
  if (strstr(frame, "RESET$") || strstr(frame, "reset$"))
    ESP.restart();
}

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
      if (bufferSize < 255)
        bufferRX[bufferSize++] = b;
  }
}

//manejador de conexiones entrantes (servidor telnet)
//atiende nuevos clientes
//envía mensaje y rechaza en caso de no poder atender la conexión
void check4NewClients(void){
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
        client.flush();
      }
    } else {  //el servicio no está en línea
        WiFiClient _client = server->available();
        _client.println("BBS fuera de línea");  //mensaje de "fuera de línea"
        _client.println("Intente en otro momento.");
        delay(0);
        _client.stop();      
    }
  }  
}

bool tcp2Serial(void){
  const int bufferSize = 2048;
  uint16_t available;
  uint8_t buffer[bufferSize];
  static long lastRXTX = millis();

//TCP -> telnetBuffer
  if (clientConnected()){  //hay cliente conectado?
    if (available = client.available()){  //y datos recibidos?
      if (available = Utils::Min(bufferSize, available, telnetBuffer.room())){  //limitamos a tamaño de buffers
        client.read(buffer, available);   //leemos los bytes
        telnetBuffer.write((const char *)buffer, available);  //almacenamos en buffer intermedio
        lastRXTX = millis();
      }
    }
  } else
    digitalWrite(DCD_PIN, HIGH);

//telnetBuffer -> Serial
  if (available = telnetBuffer.available()){  //hay datos para enviar por puerto serial?
    if (available = Utils::Min(128, available, Serial.availableForWrite())){  //limitamos a tamaño de buffers
      telnetBuffer.read((char *)buffer, available);  //leemos los bytes
      Serial.write(buffer, available);  //enviamos por puerto serial
      lastRXTX = millis();
    }
  }

  return millis() - lastRXTX > 100;
}

//recepción de tramas por puerto serial y reenvío a cliente TCP
bool serial2TCP(void){
  const int bufferSize = 512;  //tamaño de buffer local (temporal)
  uint16_t available;
  uint8_t buffer[bufferSize];  //buffer temporal
  long rxWindowMillis;

//Serial -> serialBuffer
//inicio de ventana de rx
//el buffer de RX es de 128bytes por lo que se utiliza la señal CTS del puerto serie para el control de flujo
//se abre ventana de recepción y se recibe hasta agotar buffer (serialBuffer) o hasta timeout (no haya datos disponibles)
  digitalWrite(CTS_PIN, HIGH);
  rxWindowMillis = millis();
  while (millis() - rxWindowMillis < 100){  //ventana de 100ms desde la última recepción
    if (available = Serial.available()){  //si hay datos disponibles en buffer de RX de Serial
      if (available = Utils::Min(available, bufferSize, serialBuffer.room())){  //limitamos a tamaño de buffers
        Serial.readBytes(buffer, available);  //leemos los bytes
        serialBuffer.write((const char *)buffer, available);  //escribimos en buffer intermedio
        rxWindowMillis = millis();  //nueva referencia de tiempo
      }
    }    
    if (serialBuffer.room() < 512)  //si buffer intermedio con poca capacidad
      digitalWrite(CTS_PIN, LOW);  //cerramos la ventana (señal al fbb para que deje de transmitir)
    delay(0);  //yield
  }
  digitalWrite(CTS_PIN, LOW);  //cerramos la ventana (señal al fbb para que deje de transmitir)
//fin de ventana

//serialBuffer -> TCP / processSerialFrame
//se reenvían todos los datos reibidos y almacenados en el buffer intermedio "serialBuffer" al cliente tcp conectado
//en caso de que no haya cliente conectado -> se procesa la trama recibida en busca de comandos de configuración
  while (available = serialBuffer.available()){  //mientras haya bytes para enviar en el buffer
    available = Utils::Min(bufferSize, available);  //limitamos a tamaño de buffers
    serialBuffer.read((char *)buffer, available);  //leemos los bytes del buffer intermedio
    if (clientConnected()){  //si hay cliente conectado
        client.write((const uint8_t *)buffer, available);  //enviamos trama al cliente
        delay(0);  //yield (para que la librería trabaje)
        lastClientActivity = millis();
    } else  //si no hay cliente
      processSerialFrame(buffer, available);  //procesamos como trama de comandos
  }
  return true;
}

//loop de la aplicación
void loop() {
  static uint8_t step = 0;

  //máquina de estados
  switch (step){
    case 0:
      if (tcp2Serial())
        step++;
    case 1:
      if (serial2TCP())
        step++;
    default:
      check4NewClients();
      step = 0;
  }

//desconexión por inactividad / fuera de línea
  if (clientConnected() && (millis() - lastClientActivity > 1000UL * g_config.clientInactivityTimeout || !online))
    client.stop();

//reinicio por inactividad
  if (millis() - lastClientActivity > 3600000)
    ESP.restart();  
}
