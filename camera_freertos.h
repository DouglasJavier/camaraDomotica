#include <Arduino.h>

#include "src/OV2640.h"
#include <WebServer.h>

#define APP_CPU 1
#define PRO_CPU 0


// Select camera model
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"
extern const char *passwordESP;

bool compararPasswords(String hashComparar);
void mjpegCB(void* pvParameters);
void camCB(void* pvParameters);
char* allocateMemory(char* aPtr, size_t aSize);
void handleJPGSstream(void);
void streamCB(void * pvParameters) ;
void handleJPG(void);
void handleNotFound();
void setup_cam();