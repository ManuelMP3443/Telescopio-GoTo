# Telescopio-GoTo
**Sistema de apuntado computarizado "GoTo" para telescopios, controlado por Horizonte(app_movil) a traves de Bluetooth.**

Este repositorio contiene todo el software, hardware y documentacion del proyecto

# Estado Actual del proyecto (Octubre 2025)
> ⚠️ **Importante:** Este es un proyecto en desarrollo activo.

* **✅ Software (App Móvil):** La aplicación de Flutter está en una fase funcional. Se conecta al servidor, muestra los datos y tiene la lógica de cálculo y envío de coordenadas implementada.

* **✅ Software (Servidor):** El backend con Django y la API REST están funcionales para servir los datos de los astros desde una base de datos.

* **❌ Hardware:** La parte física (la montura del telescopio, los motores, el circuito con Arduino) **aún no ha sido construida ni integrada**. El código de Arduino y los diagramas en el repositorio son versiones funcionales para pruebas y diseño.


## Estructura del Repositorio

* **`/Software`**: Contiene todo el código fuente.
     * `/Horizonte`: Sistema que incluye la app como el servidor
         * `/app`: aplicacion móvil "Horizonte" desarrollada en Flutter que sirve como interfaz de control.
         * `/server`: El backend en Django/Python que gestiona la base de datos de objetos astronómicos y sirve la API.
* **`/Hardware`**: Contiene los archivos de diseño y firmware para la parte física.
    * `/codigo_arduino`: Sketches de Arduino para el control de motores y la comunicación.
    * `/diagramas_circuito`: Esquemas de conexión de los componentes electrónicos.
* **`/Documentacion`**: Contiene los documentos conceptuales y de planificación del proyecto.
  
## 🛠️ Tecnologías Utilizadas

* **Frontend (App Móvil):**
    * Framework: Flutter
    * Lenguaje: Dart
    * Comunicación: `flutter_bluetooth_serial`
    * Ubicación: `geolocator`
    * Peticiones a servidor: `http`

* **Backend (Servidor API):**
    * Framework: Django & Django REST Framework
    * Lenguaje: Python
    * Base de Datos: MySQL (o SQLite para desarrollo)

* **Hardware (Controlador):**
    * Plataforma: Arduino
    * Lenguaje: C/C++
    * Comunicación: Módulo Bluetooth HC-06
    * Actuadores: Motores a Pasos NEMA 17 con drivers A4988




