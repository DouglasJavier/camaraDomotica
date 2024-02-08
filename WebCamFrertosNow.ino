
#include <WiFi.h>
#include "camera_freertos.h"
#include "soc/rtc_cntl_reg.h"
#include "ArduinoJson.h"
#include <EEPROM.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
const char *ssid = "COLQUE";
const char *password = "A9S8D7F6XYZ";
/* const char *ssid = "TECNO SPARK Go 2023";
const char *password = "rshniq4rwwfkqd7"; */
const char *serverAddress = "http://192.168.0.18:5000/historialIncidentes";

WebServer server(80);
uint8_t broadcastAddress[] = { 0xEC, 0x62, 0x60, 0x93, 0x02, 0x60 };

struct PinInfo {
  int pin;
  String tipo;
  String descripcion;
  int idUbicacion;
};
struct SensorInfo {
  int pin;
  int detecciones;
};
std::vector<PinInfo> pinInfoList;
std::vector<SensorInfo> sensoresActivos;
String idDispositivo;
boolean alumbradoAutomatico;

//devolucion de la llamada cuando se envian datos
TaskHandle_t tSensor;  // maneja entradas
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  setup_wifi();
  server.on("/conf_pin", HTTP_POST, handlePostSensorActuador);
  server.on("/actuador", HTTP_POST, handleActuador);
  server.on("/sensores", HTTP_POST, handlePostSensoresActivos);
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
  xTaskCreatePinnedToCore(
    leerSensor,
    "Sensores",
    2 * 1024,
    NULL,
    2,
    &tSensor,
    1);
}
void loop() {
}

void handlePostSensorActuador() {
  // Obtener el cuerpo de la solicitud POST
  Serial.begin(115200);
  /* Serial.println("Entró aqui"); */
  String json = server.arg("plain");

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
    String descripcion = item["descripcion"].as<String>();
    int idUbicacion = item["idUbicacion"].as<int>();
    Serial.print("Pin : ");
    Serial.print(pin);
    Serial.print(" ");
    Serial.println(tipo);
    // Configurar el pin según el tipo (SENSOR o ACTUADOR)
    if (tipo == "SENSOR") {
      pinMode(pin, INPUT);
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
    pinInfo.descripcion = descripcion;
    pinInfo.idUbicacion = idUbicacion;
    pinInfoList.push_back(pinInfo);
  }
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
  alumbradoAutomatico = doc["alumbradoAutomatico"].as<boolean>();

  // Recorrer el arreglo y configurar los pines
  Serial.println("Pines de senores que deben activarse: ");
  for (JsonObject item : sensoresActuadores) {
    int pin = item["pin"].as<int>();
    /* Serial.println("Entro al bucle"); */
    // Configurar el pin según el tipo (SENSOR o ACTUADOR)
    // Crear una instancia de PinInfo y agregarla al vector
    Serial.print("Pin : ");
    Serial.println(pin);
    SensorInfo sensorInfo;
    sensorInfo.pin = pin;
    sensorInfo.detecciones = 0;
    sensoresActivos.push_back(sensorInfo);
  }

  guardarSensoresActivosEnArchivo();
  imprimirSensoresActivosArchivo();
  // Enviar una respuesta JSON de confirmación
  String response = "{\"message\":\"Pin configurado correctamente\"}";
  server.send(200, "application/json", response);
}

void guardarConfiguracionEnArchivo() {
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

  // Crear un arreglo JSON para los pines
  JsonArray pinArray = configDoc.createNestedArray("pines");

  // Agregar cada pin al arreglo
  for (const SensorInfo &pinInfo : sensoresActivos) {
    JsonObject pinConfig = pinArray.createNestedObject();
    pinConfig["pin"] = pinInfo.pin;
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
      pinInfo.descripcion = pinConfig["descripcion"].as<String>();
      pinInfo.idUbicacion = pinConfig["idUbicacion"];

      Serial.print(pinInfo.pin);
      Serial.print(" ");
      Serial.println(pinInfo.tipo);
      // Configurar el pin según el tipo (SENSOR o ACTUADOR)
      if (pinInfo.tipo == "SENSOR") {
        pinMode(pinInfo.pin, INPUT);
      } else if (pinInfo.tipo == "ACTUADOR") {
        pinMode(pinInfo.pin, OUTPUT);
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

    JsonArray pinArray = configDoc["pines"].as<JsonArray>();

    // Recorrer el arreglo de pines y cargarlos en la lista
    for (JsonObject pinConfig : pinArray) {
      SensorInfo sensorInfo;
      sensorInfo.pin = pinConfig["pin"];
      sensorInfo.detecciones = pinConfig["detecciones"];
      // Agregar el pin a la lista
      sensoresActivos.push_back(sensorInfo);
    }
  }
}
void leerSensor(void *parameters) {
  while (1) {

    unsigned long ultimaHoraEnviada = 0;
    for (SensorInfo &sensorInfo : sensoresActivos) {
      //Serial.println("leendo sensor");
      // Verificar si el pin de sensor está activo (cambiar según tu lógica)
      int valorSensor = digitalRead(sensorInfo.pin);
      if (valorSensor == HIGH) {
        if ((sensorInfo.detecciones < 100) && (millis() - ultimaHoraEnviada >= 30000)) {
          sensorInfo.detecciones = sensorInfo.detecciones + 1;  // Incrementar el contador de detecciones
          Serial.print(sensorInfo.detecciones);
          Serial.println("..SI");
        }
      } /*  else {
        sensorInfo.detecciones = 0;  // Reiniciar el contador si no se detecta nada
      } */

      // Si se detecta tres veces, enviar JSON
      if (alumbradoAutomatico) {
        activarAlumbradoAutomatico(sensorInfo.pin, valorSensor);
      }
      if ((sensorInfo.detecciones >= 100) && (millis() - ultimaHoraEnviada >= 30000)) {
        sendSensorData(sensorInfo);
        sensorInfo.detecciones = 0;  // Reiniciar el contador después de enviar
        delay(30000);
      }
    }
  }
}
void handleActuador() {
  String json = server.arg("plain");

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
    digitalWrite(pin, HIGH);  // Encender el actuador
  } else if (accion == "APAGAR") {
    digitalWrite(pin, LOW);  // Apagar el actuador
  } else {
    server.send(400, "application/json", "{\"message\":\"Acción no válida\"}");
    return;
  }

  // Enviar una respuesta JSON de confirmación
  String response = "{\"message\":\"Acción realizada correctamente\"}";
  server.send(200, "application/json", response);
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
  WiFi.begin(ssid, password);
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
  int  ubicacion = buscarUbicacion(pin);
  int tam = sizeof(pinInfoList) / sizeof(pinInfoList[0]);
  for (int i = 0; i < tam; i++) {
    PinInfo pin = pinInfoList[i];
    if (pin.descripcion == "FOCO" && pin.idUbicacion == ubicacion) {
      digitalWrite(pin.pin, estado);
    }
  }
}

int buscarUbicacion(int pin) {
  int tam = sizeof(pinInfoList) / sizeof(pinInfoList[0]);
  int ubicacion = 0;
  for (int i = 0; i < tam; i++) {
    if(pinInfoList[i].pin == pin)
    ubicacion = pinInfoList[i].idUbicacion;
  }
  return ubicacion;
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
    Serial.print("Descripción: ");
    Serial.println(pinInfo.descripcion);
    Serial.print("ID de Ubicación: ");
    Serial.println(pinInfo.idUbicacion);
  }
}