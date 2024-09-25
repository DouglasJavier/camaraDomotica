# Description

This code for the ESP32-CAM is part of the **smart home security system**, where the ESP32-CAM performs the following functions:

- **Camera Streaming**: streams video in MJPEG format to multiple clients and captures images in JPG format. It functions as a web server using FreeRTOS to handle multiple tasks simultaneously.
- **Sensor and Actuator Configuration**: allows configuring pins as inputs or outputs via JSON, and stores these configurations persistently.
- **Actuators**: controls output pins, turning them on or off through commands in JSON format.
- **Active Sensors**: monitors configured pins and sends alerts when a sensor detects an event.
- **WiFi Connection**: connects the device to the WiFi network and configures the APIs for the modules. Additionally, it ensures connection security by requiring a SHA256 encrypted key.

You can find connection examples in the `imgs` folder.

## Related Repositories

Other parts of the project can be found at:
- Backend: [domotica-backend-nestjs](https://github.com/DouglasJavier/domotica-backend-nestjs)
- Frontend: [domotica-frontend-react-vite](https://github.com/DouglasJavier/domotica-frontend-react-vite)

## Note

This project is based on the "[esp32-cam-mjpeg-multiclient](https://github.com/arkhipenko/esp32-cam-mjpeg-multiclient)" repository by Anatoli Arkhipenko. The code was adapted and modularized to meet the specific requirements of our system.

# Descripción

Este código para la ESP32-CAM es parte del **sistema domótico de seguridad**, donde la ESP32-CAM cumple con las siguientes funciones:

- **Cámara Streaming**: transmite video en formato MJPEG a múltiples clientes y captura imágenes en formato JPG. Funciona como un servidor web utilizando FreeRTOS para manejar múltiples tareas simultáneamente.
- **Configuración de Sensores y Actuadores**: permite configurar pines como entradas o salidas a través de JSON, y almacena estas configuraciones de manera persistente.
- **Actuadores**: permite controlar los pines de salida, encendiéndolos o apagándolos mediante comandos en formato JSON.
- **Sensores Activos**: monitorea los pines configurados y envía alertas cuando un sensor detecta un evento.
- **Conexión WiFi**: conecta el dispositivo a la red WiFi y configura las APIs de los módulos. Además, garantiza la seguridad de la conexión mediante una clave encriptada con SHA256.

Puedes encontrar ejemplos de conexión en la carpeta `imgs`.

## Repositorios relacionados
Las demas partes del proyecto se encuentran en:
- Backend: [domotica-backend-nestjs](https://github.com/DouglasJavier/domotica-backend-nestjs)
- Frontend: [domotica-frontend-react-vite](https://github.com/DouglasJavier/domotica-frontend-react-vite)

## Nota

Este proyecto se basa en el repositorio "[esp32-cam-mjpeg-multiclient](https://github.com/arkhipenko/esp32-cam-mjpeg-multiclient)" de Anatoli Arkhipenko. El código fue adaptado y modularizado para cumplir con los requisitos específicos de nuestro sistema.
