
#include "camera_freertos.h"


OV2640 cam;

extern WebServer server;

TaskHandle_t tMjpeg;   // maneja las conexiones del cliente al servidor web
TaskHandle_t tCam;     // gestiona la obtención de fotogramas de la cámara y su almacenamiento local
TaskHandle_t tStream;  // en realidad transmite fotogramas a todos los clientes conectados

// El semáforo frameSync se usa para evitar el búfer de transmisión, ya que se reemplaza con el siguiente cuadro
SemaphoreHandle_t frameSync = NULL;

// La cola almacena los clientes actualmente conectados a los que estamos transmitiendo
QueueHandle_t streamingClients;

// Intentaremos lograr una velocidad de fotogramas de 25 FPS
const int FPS = 14;

// Manejaremos solicitudes de clientes web cada 50 ms (20 Hz)
const int WSINTERVAL = 100;


// ======== Tarea del controlador de conexión del servidor ==========================
void mjpegCB(void* pvParameters) {
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(WSINTERVAL);

  // Creando un semáforo de sincronización de tramas e inicializándolo
  frameSync = xSemaphoreCreateBinary();
  xSemaphoreGive(frameSync);

  // Creando una cola para rastrear todos los clientes conectados
  streamingClients = xQueueCreate(10, sizeof(WiFiClient*));

  //=== sección de configuración ==================

  // Crear una tarea RTOS para capturar fotogramas de la cámara
  xTaskCreatePinnedToCore(
    camCB,     // callback
    "cam",     // name
    6 * 1024,  // stacj size
    NULL,      // parameters
    5,         // priority
    &tCam,     // RTOS task handle
    APP_CPU);  // core

  // Creando una tarea para enviar la transmisión a todos los clientes conectados
  xTaskCreatePinnedToCore(
    streamCB,
    "strmCB",
    6 * 1024,
    NULL,  //(void*) handler,
    5,
    &tStream,
    APP_CPU);

  // Registro de rutinas de manejo del servidor web
  server.on("/mjpeg", HTTP_GET, handleJPGSstream);
  server.on("/jpg", HTTP_GET, handleJPG);

  server.onNotFound(handleNotFound);

  // Iniciando el servidor web

  //=== loop() section  ===================
  xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    server.handleClient();

    // Después de cada solicitud de manejo del cliente del servidor, dejamos que se ejecuten otras tareas y luego hacemos una pausa
    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}


// Variables de uso común:
volatile size_t camSize;  // tamaño del cuadro actual, byte
volatile char* camBuf;    // puntero al cuadro actual


// ==== Tarea RTOS para tomar fotogramas de la cámara =========================
void camCB(void* pvParameters) {

  TickType_t xLastWakeTime;

  // Un intervalo de ejecución asociado con la velocidad de fotogramas deseada actualmente
  const TickType_t xFrequency = pdMS_TO_TICKS(1000 / FPS);

  // Mutex para la sección crítica de cambiar los marcos activos
  portMUX_TYPE xSemaphore = portMUX_INITIALIZER_UNLOCKED;

  // Punteros a los 2 cuadros, sus respectivos tamaños e índice del cuadro actual
  char* fbs[2] = { NULL, NULL };
  size_t fSize[2] = { 0, 0 };
  int ifb = 0;

  //=== loop() section  ===================
  xLastWakeTime = xTaskGetTickCount();

  for (;;) {

    // Toma un cuadro de la cámara y consulta su tamaño
    cam.run();
    //Serial.println('tomando frame camara');
    size_t s = cam.getSize();
    // Si el tamaño del marco es mayor que el que hemos asignado previamente, solicite el 125% del espacio del marco actual
    if (s > fSize[ifb]) {
      fSize[ifb] = s * 4 / 3;
      fbs[ifb] = allocateMemory(fbs[ifb], fSize[ifb]);
    }

    // Copia el cuadro actual en el búfer local
    char* b = (char*)cam.getfb();
    memcpy(fbs[ifb], b, s);

    // Permita que otras tareas se ejecuten y espere hasta el final del intervalo de velocidad de fotogramas actual (si queda tiempo)
    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    // Cambiar fotogramas solo si ningún fotograma se está transmitiendo actualmente a un cliente
    // Espere en un semáforo hasta que se complete la operación del cliente
    xSemaphoreTake(frameSync, portMAX_DELAY);

    // No permitir interrupciones al cambiar el cuadro actual
    portENTER_CRITICAL(&xSemaphore);
    camBuf = fbs[ifb];
    camSize = s;
    ifb++;
    ifb &= 1;  // esto debería producir 1, 0, 1, 0, 1 ... secuencia
    portEXIT_CRITICAL(&xSemaphore);

    // Que cualquier persona que esté esperando un marco sepa que el marco está listo
    xSemaphoreGive(frameSync);

    // Técnicamente solo se necesita una vez: informe a la tarea de transmisión que tenemos al menos un cuadro
    // y podría comenzar a enviar marcos a los clientes, si los hay
    xTaskNotifyGive(tStream);

    // Permitir que se ejecuten inmediatamente otras tareas (transmisión)
    taskYIELD();

    // Si la tarea de transmisión se ha suspendido sola (no hay clientes activos a los que transmitir)
    // no hay necesidad de tomar fotogramas de la cámara. Podemos ahorrar un poco de jugo
    // suspendiendo las tareas
    if (eTaskGetState(tStream) == eSuspended) {
      vTaskSuspend(NULL);  // passing NULL means "suspend yourself"
    }
  }
}


// ==== Asignador de memoria que aprovecha PSRAM si está presente =======================
char* allocateMemory(char* aPtr, size_t aSize) {

  // Dado que el búfer actual es demasiado pequeño, libéralo
  if (aPtr != NULL) free(aPtr);


  size_t freeHeap = ESP.getFreeHeap();
  char* ptr = NULL;

  // Si la memoria solicitada es más de 2/3 del montón libre actualmente, intente PSRAM inmediatamente
  if (aSize > freeHeap * 2 / 3) {
    if (psramFound() && ESP.getFreePsram() > aSize) {
      ptr = (char*)ps_malloc(aSize);
    }
  } else {
    // Suficiente montón libre: intentemos asignar RAM rápida como búfer
    ptr = (char*)malloc(aSize);

    // Si falla la asignación en el montón, demos a PSRAM una oportunidad más:
    if (ptr == NULL && psramFound() && ESP.getFreePsram() > aSize) {
      ptr = (char*)ps_malloc(aSize);
    }
  }

  // Finalmente, si el puntero de memoria es NULL, no pudimos asignar ninguna memoria, y esa es una condición terminal.
  if (ptr == NULL) {
    ESP.restart();
  }
  return ptr;
}


// ==== STREAMING ======================================================
const char HEADER[] = "HTTP/1.1 200 OK\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Content-Type: multipart/x-mixed-replace; boundary=123456789000000000000987654321\r\n";
const char BOUNDARY[] = "\r\n--123456789000000000000987654321\r\n";
const char CTNTTYPE[] = "Content-Type: image/jpeg\r\nContent-Length: ";
const int hdrLen = strlen(HEADER);
const int bdrLen = strlen(BOUNDARY);
const int cntLen = strlen(CTNTTYPE);

// ==== Manejar la solicitud de conexión de los clientes ===============================
void handleJPGSstream(void) {
  String key = server.header("Authorization").substring(7);
  if (!compararPasswords(key)) {
    Serial.println("Desautorizado");
    String response = "{\"message\":\"Desautorizado\"}";
    server.send(401, "application/json", response);
    return;
  }
  /* int queueSize =  uxQueueMessagesWaiting(streamingClients);
    Serial.print("Tamaño de la cola: ");
    Serial.println(queueSize); */
  // Solo puede acomodar 10 clientes. El límite es un valor predeterminado para las conexiones WiFi
  if (!uxQueueSpacesAvailable(streamingClients)) return;


  // Cree un nuevo objeto de cliente WiFi para realizar un seguimiento de este
  WiFiClient* client = new WiFiClient();
  *client = server.client();

  // Inmediatamente enviar a este cliente un encabezado
  client->write(HEADER, hdrLen);
  client->write(BOUNDARY, bdrLen);

  // Empuje al cliente a la cola de transmisión
  xQueueSend(streamingClients, (void*)&client, 0);

  // Activar tareas de streaming, si estaban suspendidas previamente:
  if (eTaskGetState(tCam) == eSuspended) vTaskResume(tCam);
  if (eTaskGetState(tStream) == eSuspended) vTaskResume(tStream);
}



// ==== En realidad transmita contenido a todos los clientes conectados ========================
void streamCB(void* pvParameters) {
  char buf[16];
  TickType_t xLastWakeTime;
  TickType_t xFrequency;

  // Espere hasta que se capture el primer cuadro y haya algo para enviar
  // a los clientes
  ulTaskNotifyTake(pdTRUE,         /* Borrar el valor de la notificación antes de salir. */
                   portMAX_DELAY); /* Bloquear indefinidamente. */

  xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    // Suposición predeterminada de que estamos ejecutando de acuerdo con el FPS
    xFrequency = pdMS_TO_TICKS(1000 / FPS);

    // Solo molestarse en enviar algo si hay alguien mirando
    UBaseType_t activeClients = uxQueueMessagesWaiting(streamingClients);
    if (activeClients) {
      // Ajustar el periodo al número de clientes conectados
      xFrequency /= activeClients;

      // Ya que estamos enviando el mismo marco a todos,
      // pop un cliente desde el frente de la colaqueue
      WiFiClient* client;
      xQueueReceive(streamingClients, (void*)&client, 0);

      // Compruebe si este cliente todavía está conectado.

      if (!client->connected()) {
        // elimina esta referencia de cliente si se ha desconectado
        // y no lo vuelvas a poner en la cola nunca más. ¡Adiós!
        delete client;
      } else {

        //  OK. Este es un cliente conectado activamente.
        // Tomemos un semáforo para evitar cambios de marco mientras
        // están sirviendo este marco
        xSemaphoreTake(frameSync, portMAX_DELAY);

        client->write(CTNTTYPE, cntLen);
        sprintf(buf, "%d\r\n\r\n", camSize);
        client->write(buf, strlen(buf));
        client->write((char*)camBuf, (size_t)camSize);
        client->write(BOUNDARY, bdrLen);

        // Dado que este cliente aún está conectado, empújelo hasta el final
        // de la cola para su posterior procesamiento
        xQueueSend(streamingClients, (void*)&client, 0);

        // El marco ha sido servido. Suelte el semáforo y deje que se ejecuten otras tareas.
        // Si hay un cambio de cuadro listo, sucederá ahora entre cuadros
        xSemaphoreGive(frameSync);
        taskYIELD();
      }
    } else {
      // Dado que no hay clientes conectados, no hay razón para desperdiciar la batería funcionando
      vTaskSuspend(NULL);
    }
    // Permitir que otras tareas se ejecuten después de atender a cada cliente
    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}




const char JHEADER[] = "HTTP/1.1 200 OK\r\n"
                       "Content-disposition: inline; filename=capture.jpg\r\n"
                       "Content-type: image/jpeg\r\n\r\n";
const int jhdLen = strlen(JHEADER);

/* const char JHEADER2[] = "HTTP/1.1 200 OK\r\n"
                        "Content-type: text/plain\r\n\r\n";
const int jhdLen2 = strlen(JHEADER2); */

// ==== Serve up one JPEG frame =============================================
void handleJPG(void) {

  WiFiClient client = server.client();
  String key = server.header("Authorization").substring(7);
  if (!compararPasswords(key)) {
    Serial.println("Desautorizado");
    String response = "{\"message\":\"Desautorizado\"}";
    server.send(401, "application/json", response);
    return;
  }
  if (!client.connected()) return;
  //vTaskSuspend(tMjpeg);
  
   if (eTaskGetState(tCam) == eSuspended) {
    cam.run();
    client.write(JHEADER, jhdLen);
    client.write((char*)cam.getfb(), cam.getSize());
  } else {
     Serial.println("Clientes en stream");
    client.write(JHEADER, jhdLen);
    client.write((char*)cam.getfb(), cam.getSize());
   
  }

  //vTaskResume(tMjpeg);
}

// ==== Handle invalid URL requests ============================================
void handleNotFound() {
  String message = "Server is running!\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  server.send(200, "text / plain", message);
}

void setup_cam() {


  // Configurar la cámara
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //config.pixel_format = PIXFORMAT_GRAYSCALE;

  // Frame parameters: pick one
  // config.frame_size = FRAMESIZE_UXGA;
  config.frame_size = FRAMESIZE_SXGA;
  //config.frame_size = FRAMESIZE_SVGA;
  //  config.frame_size = FRAMESIZE_QVGA;
  /* config.frame_size = FRAMESIZE_VGA; */
  config.jpeg_quality = 12;
  config.fb_count = 2;

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  if (cam.init(config) != ESP_OK) {
    Serial.println("Error initializing the camera");
    delay(10000);
    ESP.restart();
  }
  xTaskCreatePinnedToCore(
    mjpegCB,
    "mjpeg",
    6 * 1024,
    NULL,
    5,
    &tMjpeg,
    APP_CPU);
}