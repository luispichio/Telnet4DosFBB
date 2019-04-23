# Telnet4DosFBB
Cliente / Servidor Telnet para DosFBB

# Limitaciones del sistema

# Conexionado
El conversor emula (y utiliza) todas las señales de control de un módem estándar.
El cable que debe utilizarse es un DTE <-> DCE con conexión pin a pin (sin inversiones) de los pines ...
 
# Configuración

## APPEL.BAT
Debe incluír la inicialización del driver fbbios.
Para mayor información consultar la documentación oficial.

```
fbbios 1 03F8 4
serv %1
```
## INITTNCx.SYS
Permite configurar parámetros de inicialización del conversor, tales como:
1. SSID y Contraseña de WiFi
2. DDNS (duckdns.org)
3. Puerto de escucha de servidor telnet
4. Estado activo / inactivo (fuera de servicio) del servidor telnet
5. Reinicio del terminal
```
#
# SSID$$ssid
# "ssid" -> SSID del WiFi a conectar
# La configuración se guarda en el Arduino por lo que es conveniente comentar la
# línea luego del primer uso (hay riesgo esporádico de salida de comandos por
# tcp exponiendo SSID y clave del WiFi)
#
#SSID$$ssid
#
# PASS$$password
# "password" -> Contraseña de la red de WiFi
#
#PASS$$password
#
# PORT$$6300
# Puerto de Telnet (conexiones entrantes)
#
#PORT$$6300
#
# Comando de reinicio (...)
#
RESET$$
#
```

## Archivo de forward
```
C C LW6DIO ^MC$$lw6dio.duckdns.org:6300^M
```

## Parámetros configurables desde el Firmware (código fuente)

```
// Modo depuración
#define DEBUG_MODE
#define DEBUG_BAUD_RATE                             57600

// Generales
#define SERIAL_BAUDRATE                             57600
#define DEFAULT_TELNET_PORT                         6300
#define WIFI_DISCONNECTED_RESET_TIMEOUT_MINUTES     5.0f
#define WITHOUT_ACTIVITY_RESET_TIMEOUT_MINUTES      90.0f
#define DEFAULT_CLIENT_INACTIVITY_TIMEOUT_SECONDS   60

// Parámetros internos (no se recomienda la modificación)

#define SERIAL_RX_BUFFER_SIZE                       4096
#define SERIAL_RX_BUFFER_CTS_TARGET                 3072

#define TCP_2_SERIAL_BUFFER_SIZE                    8192
#define TCP_RX_FRAME_TIMEOUT                        50
#define SERIAL_2_TCP_BUFFER_SIZE                    1500  //1460?
#define SERIAL_RX_FRAME_TIMEOUT                     50
```

# Referencias
http://www.f6fbb.org/fbbdoc/doc.htm