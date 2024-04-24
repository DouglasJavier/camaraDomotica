
#include <WiFi.h>
#include <esp_wifi.h>
#include "camera_freertos.h"
#include "soc/rtc_cntl_reg.h"
#include "ArduinoJson.h"
#include <EEPROM.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <mbedtls/sha256.h>
#include "esp_heap_caps.h"

/* const char *ssid = "SR COLQUE";
const char *password = "a9s8d7f6XYZ"; */
const char *ssid = "COLQUE";
const char *password = "A9S8D7F6XYZ";
/* const char *ssid = "TECNO SPARK Go 2023";
const char *password = "rshniq4rwwfkqd7"; */
/* const char *ssid = "AGETIC01";
const char *password = "03r1XY6mOT$"; */
IPAddress ip(192, 168, 0, 202);      // Asigna la IP estática deseada
IPAddress gateway(192, 168, 0, 1);   // Asigna la puerta de enlace (router)
IPAddress subnet(255, 255, 255, 0);  // Asigna la máscara de subred
/* IPAddress ip(192, 168, 29, 250);     // Asigna la IP estática deseada
IPAddress gateway(192, 168, 29, 1);  // Asigna la puerta de enlace (router)
IPAddress subnet(255, 255, 254, 0);  // Asigna la máscara de subred */

const char *serverAddress = "http://192.168.0.16:5000/historialIncidentes";
/* const char *serverAddress = "https://domotica-backend-nestjs.onrender.com/historialIncidentes"; */
const char *passwordESP = "your_password";
char hash[65];
String hashString;

WebServer server(80);
uint8_t broadcastAddress[] = { 0xEC, 0x62, 0x60, 0x93, 0x02, 0x60 };

struct PinInfo {
  int pin;
  String tipo;
  String tipoSalida;
  String descripcion;
  int idUbicacion;
};
struct SensorInfo {
  int pin;
  int detecciones;
  String tipoSalida;
};

struct SensorFocoMapping {
  int pinSensor;
  int pinFoco;
  int idUbicacion;
};

std::vector<PinInfo> pinInfoList;
std::vector<SensorInfo> sensoresActivos;
std::vector<SensorFocoMapping> sensorFocoMapa;
String idDispositivo;
boolean alumbradoAutomatico;
boolean enviarReporte;

//devolucion de la llamada cuando se envian datos
TaskHandle_t tSensor;     // maneja entradas
TaskHandle_t tVerificar;  // maneja la reconeccion
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  cifrarSHA256(passwordESP, strlen(passwordESP), hashString);
  setup_wifi();
  server.on("/conf_pin", HTTP_POST, handlePostSensorActuador);
  server.on("/actuador", HTTP_POST, handleActuador);
  server.on("/sensores", HTTP_POST, handlePostSensoresActivos);
  server.on("/alumbradoAutomatico", HTTP_POST, handleAlumbradoAutomatico);
  server.on("/reiniciar", HTTP_POST, handleReiniciar);
  setup_cam();
  server.begin();
  if (!SPIFFS.begin(true)) {
    Serial.println("Error al inicializar SPIFFS.");
    return;
  }
  cargarConfiguracionDesdeArchivo();
  imprimirConfiguracionArchivo();
  cargarSensoresActivosDesdeArchivo();
  imprimirSensoresActivosArchivo();
  imprimirPinInfoList();
  // creando mapa
  crearMapaSensorFoco();
  xTaskCreatePinnedToCore(
    leerSensor,
    "Sensores",
    4 * 1024,
    NULL,
    2,
    &tSensor,
    0);
  xTaskCreatePinnedToCore(
    verificarEstado,
    "Verificar",
    2 * 1024,
    NULL,
    1,
    &tVerificar,
    1);
}
void loop() {
}

void handlePostSensorActuador() {
  // Obtener el cuerpo de la solicitud POST
  Serial.begin(115200);
  /* Serial.println("Entró aqui"); */
  String json = server.arg("plain");
  String key = server.header("Authorization").substring(7);
  if (!compararPasswords(key)) {
    Serial.println("Desautorizado");
    String response = "{\"message\":\"Desautorizado\"}";
    server.send(401, "application/json", response);
    return;
  }
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.println("Error en el JSON");
    return;
  }

  // Obtener el arreglo de sensores y actuadores
  JsonArray sensoresActuadores = doc["sensoresActuadores"].as<JsonArray>();

  idDispositivo = doc["idDispositivo"].as<String>();
  pinInfoList.clear();
  // Recorrer el arreglo y configurar los pines
  Serial.println("Pines Sensores Actuadores Configuración: ");
  for (JsonObject item : sensoresActuadores) {
    int pin = item["pin"].as<int>();
    String tipo = item["tipo"].as<String>();
    String tipoSalida = item["tipoSalida"].as<String>();
    String descripcion = item["descripcion"].as<String>();
    int idUbicacion = item["idUbicacion"].as<int>();
    Serial.print("Pin : ");
    Serial.print(pin);
    Serial.print(" ");
    Serial.println(tipo);
    Serial.print(" ");
    Serial.println(tipoSalida);
    // Configurar el pin según el tipo (SENSOR o ACTUADOR)
    if (tipo == "SENSOR") {
      if (tipoSalida == "pullUp") {
        Serial.println("sensor pull up");
        pinMode(pin, INPUT_PULLUP);
      } else {
        pinMode(pin, INPUT_PULLDOWN);
      }
    } else if (tipo == "ACTUADOR") {
      pinMode(pin, OUTPUT);
    } else {
      Serial.println("Tipo no válido");
      continue;  // Continuar con la siguiente entrada
    }

    // Crear una instancia de PinInfo y agregarla al vector
    PinInfo pinInfo;
    pinInfo.pin = pin;
    pinInfo.tipo = tipo;
    pinInfo.tipoSalida = tipoSalida;
    pinInfo.descripcion = descripcion;
    pinInfo.idUbicacion = idUbicacion;
    pinInfoList.push_back(pinInfo);
  }
  // creando mapa
  crearMapaSensorFoco();
  Serial.println("Pines configurados correctamente");
  guardarConfiguracionEnArchivo();
  imprimirConfiguracionArchivo();
  // Enviar una respuesta JSON de confirmación
  String response = "{\"message\":\"Pin configurado correctamente\"}";
  server.send(200, "application/json", response);
}

void handlePostSensoresActivos() {
  // Obtener el cuerpo de la solicitud POST
  Serial.begin(115200);
  /*  Serial.println("Entró aqui") */;
  String key = server.header("Authorization").substring(7);
  if (!compararPasswords(key)) {
    Serial.println("Desautorizado");
    String response = "{\"message\":\"Desautorizado\"}";
    server.send(401, "application/json", response);
    return;
  }
  String json = server.arg("plain");

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.println("Error en el JSON");
    return;
  }
  sensoresActivos.clear();
  // Obtener el arreglo de sensores y actuadores
  JsonArray sensoresActuadores = doc["sensoresActuadores"].as<JsonArray>();
  enviarReporte = doc["enviarReporte"].as<boolean>();
  alumbradoAutomatico = doc["alumbradoAutomatico"].as<boolean>();
  // Recorrer el arreglo y configurar los pines
  Serial.println("Pines de senores que deben activarse: ");
  for (JsonObject item : sensoresActuadores) {
    int pin = item["pin"].as<int>();
    String tipoSalida = item["tipoSalida"].as<String>();
    /* Serial.println("Entro al bucle"); */
    // Configurar el pin según el tipo (SENSOR o ACTUADOR)
    // Crear una instancia de PinInfo y agregarla al vector
    Serial.print("Pin : ");
    Serial.println(pin);
    SensorInfo sensorInfo;
    sensorInfo.pin = pin;
    sensorInfo.tipoSalida = tipoSalida;
    sensorInfo.detecciones = 0;
    sensoresActivos.push_back(sensorInfo);
  }
  if (!alumbradoAutomatico) {
    apagarAlumbradoAutomatico();
  }
  guardarSensoresActivosEnArchivo();
  imprimirSensoresActivosArchivo();
  // Enviar una respuesta JSON de confirmación
  String response = "{\"message\":\"Pin configurado correctamente\"}";
  server.send(200, "application/json", response);
}

void guardarConfiguracionEnArchivo() {
  if (SPIFFS.remove("/config.json.json")) {
    Serial.println("Archivo config.json eliminado correctamente.");
  } else {
    Serial.println("No se pudo eliminar el archivo sensoresActivos.json.");
  }

  // Abre el archivo en modo de escritura
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Error al abrir el archivo de configuración.");
    return;
  }

  // Crear un objeto JSON para almacenar la configuración
  DynamicJsonDocument configDoc(1024);

  // Agregar el ID del dispositivo a la configuración
  configDoc["idDispositivo"] = idDispositivo;

  // Crear un arreglo JSON para los pines
  JsonArray pinArray = configDoc.createNestedArray("pines");

  // Agregar cada pin al arreglo
  for (const PinInfo &pinInfo : pinInfoList) {
    JsonObject pinConfig = pinArray.createNestedObject();
    pinConfig["pin"] = pinInfo.pin;
    pinConfig["tipo"] = pinInfo.tipo;
    pinConfig["tipoSalida"] = pinInfo.tipoSalida;
    pinConfig["descripcion"] = pinInfo.descripcion;
    pinConfig["idUbicacion"] = pinInfo.idUbicacion;
  }

  // Serializar la configuración en una cadena JSON
  String configJson;
  serializeJson(configDoc, configJson);

  // Escribir la cadena JSON en el archivo
  configFile.println(configJson);
  configFile.close();
  Serial.println("La configuración se guardó correctamente");
}
void guardarSensoresActivosEnArchivo() {
  if (SPIFFS.remove("/sensoresActivos.json")) {
    Serial.println("Archivo sensoresActivos.json eliminado correctamente.");
  } else {
    Serial.println("No se pudo eliminar el archivo sensoresActivos.json.");
  }

  // Abre el archivo en modo de escritura nuevamente
  File configFile = SPIFFS.open("/sensoresActivos.json", "w");
  if (!configFile) {
    Serial.println("Error al abrir el archivo de sensores activos.");
    return;
  }

  // Crear un objeto JSON para almacenar la configuración
  DynamicJsonDocument configDoc(1024);
  configDoc["alumbradoAutomatico"] = alumbradoAutomatico;
  configDoc["enviarReporte"] = enviarReporte;

  // Crear un arreglo JSON para los pines
  JsonArray pinArray = configDoc.createNestedArray("pines");

  // Agregar cada pin al arreglo
  for (const SensorInfo &pinInfo : sensoresActivos) {
    JsonObject pinConfig = pinArray.createNestedObject();
    pinConfig["pin"] = pinInfo.pin;
    pinConfig["tipoSalida"] = pinInfo.tipoSalida;
    pinConfig["detecciones"] = pinInfo.detecciones;
  }

  // Serializar la configuración en una cadena JSON
  String configJson;
  serializeJson(configDoc, configJson);

  // Escribir la cadena JSON en el archivo
  configFile.println(configJson);
  configFile.close();
  Serial.println("Se guardó corrrectamente");
}

void cargarConfiguracionDesdeArchivo() {
  // Abre el archivo en modo de lectura
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("No se pudo abrir el archivo de configuración.");
    return;
  }

  // Lee el contenido del archivo en una cadena JSON
  String configJson = configFile.readString();
  configFile.close();

  // Analizar el JSON y cargar la configuración en pinInfoList
  DynamicJsonDocument configDoc(1024);
  DeserializationError error = deserializeJson(configDoc, configJson);

  if (!error) {
    // Obtener el ID del dispositivo
    idDispositivo = configDoc["idDispositivo"].as<String>();

    // Obtener el arreglo de pines
    JsonArray pinArray = configDoc["pines"].as<JsonArray>();
    // Recorrer el arreglo de pines y cargarlos en la lista
    for (JsonObject pinConfig : pinArray) {
      PinInfo pinInfo;
      pinInfo.pin = pinConfig["pin"];
      pinInfo.tipo = pinConfig["tipo"].as<String>();
      pinInfo.tipoSalida = pinConfig["tipoSalida"].as<String>();
      pinInfo.descripcion = pinConfig["descripcion"].as<String>();
      pinInfo.idUbicacion = pinConfig["idUbicacion"];

      Serial.print(pinInfo.pin);
      Serial.print(" ");
      Serial.println(pinInfo.tipo);
      // Configurar el pin según el tipo (SENSOR o ACTUADOR)
      if (pinInfo.tipo == "SENSOR") {
        if (pinInfo.tipoSalida == "pullUp") {
          Serial.println("sensor pull up");
          pinMode(pinInfo.pin, INPUT_PULLUP);
        } else {
          pinMode(pinInfo.pin, INPUT_PULLDOWN);
        }
      } else if (pinInfo.tipo == "ACTUADOR") {
        pinMode(pinInfo.pin, OUTPUT);
        digitalWrite(pinInfo.pin, HIGH);
      } else {
        Serial.println("Tipo no válido");
        continue;  // Continuar con la siguiente entrada
      }

      // Agregar el pin a la lista
      pinInfoList.push_back(pinInfo);
    }
    Serial.println("La configuración de cargó correctamente");
  }
}
void cargarSensoresActivosDesdeArchivo() {
  // Abre el archivo en modo de lectura
  File configFile = SPIFFS.open("/sensoresActivos.json", "r");
  if (!configFile) {
    Serial.println("No se pudo abrir el archivo de sensores activos.");
    return;
  }

  // Lee el contenido del archivo en una cadena JSON
  String configJson = configFile.readString();
  configFile.close();

  // Analizar el JSON y cargar la configuración en pinInfoList
  DynamicJsonDocument configDoc(1024);
  DeserializationError error = deserializeJson(configDoc, configJson);

  if (!error) {
    // Obtener el arreglo de pines
    alumbradoAutomatico = configDoc["alumbradoAutomatico"].as<boolean>();
    enviarReporte = configDoc["enviarReporte"].as<boolean>();

    JsonArray pinArray = configDoc["pines"].as<JsonArray>();

    // Recorrer el arreglo de pines y cargarlos en la lista
    for (JsonObject pinConfig : pinArray) {
      SensorInfo sensorInfo;
      sensorInfo.pin = pinConfig["pin"];
      sensorInfo.tipoSalida = pinConfig["tipoSalida"].as<String>();
      sensorInfo.detecciones = pinConfig["detecciones"];
      // Agregar el pin a la lista
      sensoresActivos.push_back(sensorInfo);
    }
  }
}
void leerSensor(void *parameters) {
  while (1) {
    if (enviarReporte || alumbradoAutomatico) {
      for (SensorInfo &sensorInfo : sensoresActivos) {
        //Serial.println("leendo sensor");
        int valorSensor = digitalRead(sensorInfo.pin);
        if (enviarReporte) {
          if (((valorSensor == HIGH && sensorInfo.tipoSalida == "pullDown") || (valorSensor == LOW && sensorInfo.tipoSalida == "pullUp")) && (sensorInfo.detecciones < 10)) {
            sensorInfo.detecciones = sensorInfo.detecciones + 1;  // Incrementar el contador de detecciones
            Serial.print(sensorInfo.detecciones);
            Serial.println("..SI");
          }
          if ((sensorInfo.detecciones >= 10)) {
            sendSensorData(sensorInfo);
            sensorInfo.detecciones = 0;  // Reiniciar el contador después de enviar
            vTaskDelay(pdMS_TO_TICKS(30000));
          }
        }

        if (alumbradoAutomatico) {
          activarAlumbradoAutomatico(sensorInfo.pin, valorSensor);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void verificarEstado(void *pvParameters) {
  while (1) {
    Serial.println("entro aqui");
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect();
      Serial.println("WiFi reconnecting...");
      setup_wifi();
    } else {
      Serial.println("WiFi aun conectado");
    }
    /* size_t freeRAM = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    Serial.print("Free RAM after initialization: ");
    Serial.println(freeRAM); */
    //delay(10000);  // Delay to avoid constant checking
    vTaskDelay(pdMS_TO_TICKS(60000));
  }
}

void handleActuador() {
  String json = server.arg("plain");
  String key = server.header("Authorization").substring(7);
  if (!compararPasswords(key)) {
    Serial.println("Desautorizado");
    String response = "{\"message\":\"Desautorizado\"}";
    server.send(401, "application/json", response);
    return;
  }
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    server.send(400, "application/json", "{\"message\":\"Error en el JSON\"}");
    return;
  }

  // Obtener el pin y la acción del JSON
  int pin = doc["pin"].as<int>();
  String accion = doc["accion"].as<String>();
  Serial.println("PIN ACCION");
  Serial.println(pin);
  Serial.println(accion);
  // Buscar el pin en la lista de configuración
  PinInfo *pinInfo = nullptr;
  for (PinInfo &info : pinInfoList) {
    Serial.println(info.pin);

    if (info.pin == pin) {
      pinInfo = &info;
      break;
    }
  }

  if (pinInfo == nullptr || pinInfo->tipo != "ACTUADOR") {
    server.send(400, "application/json", "{\"message\":\"Pin no encontrado o no es un actuador\"}");
    return;
  }

  // Realizar la acción correspondiente
  if (accion == "ENCENDER") {
    digitalWrite(pin, LOW);  // Encender el actuador
  } else if (accion == "APAGAR") {
    digitalWrite(pin, HIGH);  // Apagar el actuador
  } else {
    server.send(400, "application/json", "{\"message\":\"Acción no válida\"}");
    return;
  }

  // Enviar una respuesta JSON de confirmación
  String response = "{\"message\":\"Acción realizada correctamente\"}";
  server.send(200, "application/json", response);
}

void handleAlumbradoAutomatico() {
  String json = server.arg("plain");
  String key = server.header("Authorization").substring(7);
  if (!compararPasswords(key)) {
    Serial.println("Desautorizado");
    String response = "{\"message\":\"Desautorizado\"}";
    server.send(401, "application/json", response);
    return;
  }
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    server.send(400, "application/json", "{\"message\":\"Error en el JSON\"}");
    return;
  }

  // Configuración alumbrado automatico JSON
  alumbradoAutomatico = doc["alumbradoAutomatico"].as<boolean>();
  Serial.println("Alumbrado automatico");
  Serial.println(alumbradoAutomatico);
  if (!alumbradoAutomatico) {
    apagarAlumbradoAutomatico();
  }
  guardarSensoresActivosEnArchivo();
  // Enviar una respuesta JSON de confirmación
  String response = "{\"message\":\"Acción realizada correctamente\"}";
  server.send(200, "application/json", response);
}

void handleReiniciar() {
  String key = server.header("Authorization").substring(7);
  if (!compararPasswords(key)) {
    Serial.println("Desautorizado");
    String response = "{\"message\":\"Desautorizado\"}";
    server.send(401, "application/json", response);
    return;
  }
  String response = "{\"message\":\"Acción realizada correctamente\"}";
  server.send(200, "application/json", response);
  ESP.restart();
}
void sendSensorData(SensorInfo &sensorInfo) {
  StaticJsonDocument<64> jsonData;
  jsonData["idDispositivo"] = idDispositivo;
  jsonData["pin"] = String(sensorInfo.pin);

  Serial.println(serverAddress);
  // Crear una conexión HTTP
  HTTPClient http;
  http.begin(serverAddress);

  // Configurar las cabeceras de la solicitud
  http.addHeader("Content-Type", "application/json");
  http.addHeader("key", hashString);

  // Convertir el JSON a una cadena
  String jsonStr;
  serializeJson(jsonData, jsonStr);
  Serial.println(jsonStr);

  // Enviar la solicitud POST con el JSON
  int httpResponseCode = http.POST(jsonStr);

  Serial.println("Encabezados de la respuesta:");

  // Imprimir los encabezados de la respuesta
  http.writeToStream(&Serial);
  // Manejar la respuesta (puedes agregar más lógica aquí según tus necesidades)
  if (httpResponseCode > 0) {
    // Manejar la respuesta (puedes agregar más lógica aquí según tus necesidades)
    Serial.println(httpResponseCode);
    Serial.println(jsonStr);
    if (httpResponseCode == 201) {
      Serial.println("JSON enviado con éxito");
      Serial.println(httpResponseCode);
    } else {
      Serial.print("Error al enviar JSON. Código de respuesta: ");
      Serial.println(httpResponseCode);
      Serial.print("Error: ");
      Serial.println(http.errorToString(httpResponseCode).c_str());
    }
  } else {
    Serial.println("Error al enviar la solicitud");
  }
  // Liberar recursos de la conexión HTTP
  http.end();
}
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.println("Conectando a :");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  //WiFi.setSleepMode(WIFI_NONE_SLEEP);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setSleep(0);
  WiFi.begin(ssid, password);
  WiFi.config(ip, gateway, subnet);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("Wifi conectado");
  Serial.println("IP address: ");
  Serial.println(WiFi.macAddress());
  Serial.print(WiFi.localIP());
  Serial.println();
}

void activarAlumbradoAutomatico(int pin, int estado) {
  for (SensorFocoMapping &sensorFoco : sensorFocoMapa) {
    if (sensorFoco.pinSensor == pin) {
      if (estado == LOW) {
        digitalWrite(sensorFoco.pinFoco, HIGH);
      } else {
        Serial.print("alumbrado en ");
        Serial.print(sensorFoco.pinFoco);
        Serial.print(" ");
        Serial.println(pin);
        digitalWrite(sensorFoco.pinFoco, LOW);
      }
    }
  }
}

void apagarAlumbradoAutomatico() {
  for (SensorFocoMapping &sensorFoco : sensorFocoMapa) {
    digitalWrite(sensorFoco.pinFoco, HIGH);
  }
}

void imprimirConfiguracionArchivo() {
  // Abre el archivo en modo de lectura
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("No se pudo abrir el archivo de configuración.");
    return;
  }

  // Lee y muestra el contenido del archivo
  Serial.println("Contenido del archivo de configuración:");

  while (configFile.available()) {
    Serial.write(configFile.read());
  }

  configFile.close();
}
void imprimirSensoresActivosArchivo() {
  // Abre el archivo en modo de lectura
  File configFile = SPIFFS.open("/sensoresActivos.json", "r");
  if (!configFile) {
    Serial.println("No se pudo abrir el archivo de sensores Activos.");
    return;
  }

  // Lee y muestra el contenido del archivo
  Serial.println("Contenido del archivo de sensores Activos:");

  while (configFile.available()) {
    Serial.write(configFile.read());
  }

  configFile.close();
}

void imprimirPinInfoList() {
  Serial.println("Contenido de pinInfoList:");

  for (const PinInfo &pinInfo : pinInfoList) {
    Serial.print("Pin: ");
    Serial.println(pinInfo.pin);
    Serial.print("Tipo: ");
    Serial.println(pinInfo.tipo);
    Serial.print("Tipo salida: ");
    Serial.println(pinInfo.tipoSalida);
    Serial.print("Descripción: ");
    Serial.println(pinInfo.descripcion);
    Serial.print("ID de Ubicación: ");
    Serial.println(pinInfo.idUbicacion);
  }
}

void cifrarSHA256(const char *pass, size_t length, String &hashString) {
  mbedtls_sha256_context sha256Context;
  mbedtls_sha256_init(&sha256Context);
  mbedtls_sha256_starts_ret(&sha256Context, 0);  // 0 for SHA-256

  mbedtls_sha256_update_ret(&sha256Context, (const unsigned char *)pass, length);
  unsigned char output[32];
  mbedtls_sha256_finish_ret(&sha256Context, output);

  mbedtls_sha256_free(&sha256Context);

  for (int i = 0; i < sizeof(output); i++) {
    hashString += (output[i] < 0x10 ? "0" : "") + String(output[i], HEX);
  }
}

bool compararPasswords(String hashComparar) {
  if (hashString != hashComparar) {
    return false;
  }
  return true;
}

void crearMapaSensorFoco() {
  Serial.println("Creando mapa sesores actuadores ...");
  sensorFocoMapa.clear();
  for (PinInfo &pinInfo : pinInfoList) {
    if (pinInfo.descripcion == "FOCO") {
      SensorFocoMapping sensorFoco;
      sensorFoco.pinFoco = pinInfo.pin;
      sensorFoco.pinSensor = 0;
      sensorFoco.idUbicacion = pinInfo.idUbicacion;
      sensorFocoMapa.push_back(sensorFoco);
    }
  }
  for (SensorFocoMapping &sensorFoco : sensorFocoMapa) {
    for (PinInfo &pinInfo : pinInfoList) {

      if (pinInfo.descripcion == "PIR" && pinInfo.idUbicacion == sensorFoco.idUbicacion) {
        sensorFoco.pinSensor = pinInfo.pin;
        Serial.println("..........................");
        Serial.println(sensorFoco.pinFoco);
        Serial.println(sensorFoco.pinSensor);
        Serial.println(sensorFoco.idUbicacion);
        Serial.println("..........................");
      }
    }
  }
  Serial.println("Se creó mapa correctamente");
}