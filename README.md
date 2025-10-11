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
    * Comunicación: Módulo Bluetooth BLE
    * Actuadores: Motores a Pasos NEMA 17 con drivers A4988
      
## 🚀 Guía de Instalación y Puesta en Marcha

Sigue estos pasos para configurar el entorno de desarrollo completo en tu máquina.

### Prerrequisitos

Antes de empezar, asegúrate de tener instalado:
* [Git](https://git-scm.com/)
* [Flutter SDK](https://flutter.dev/docs/get-started/install) (para la app móvil)
* [Python 3.8+](https://www.python.org/downloads/) (para el servidor)
* Un servidor de **MySQL** funcionando en tu computadora.

---

### 1. Configuración del Backend (Servidor)

Estos pasos prepararán el servidor Django.

1.  **Clona el repositorio y navega a la carpeta del servidor:**
    *(Si ya lo clonaste, solo navega a la carpeta)*
    ```bash
    git clone <URL_DE_TU_REPOSITORIO>
    cd Telescopio-Goto/Software/Horizonte/server
    ```

2.  **Crea y activa el entorno virtual:**
    ```bash
    # Crear el entorno virtual
    python -m venv venv

    # Activar el entorno (en Windows)
    .\venv\Scripts\activate
    ```

3.  **Instala todas las dependencias de Python necesarias:**
    ```bash
    pip install django djangorestframework django-cors-headers mysqlclient
    ```

4.  **Configura tus credenciales locales:**
    * Dentro de la carpeta `.../server/backend/`, crea un nuevo archivo llamado `local_settings.py`.
    * Añade tus credenciales secretas en este archivo.

    ```python
    # Ejemplo de contenido para local_settings.py
    SECRET_KEY = 'tu_clave_secreta_personal_aqui'

    DB_NAME = 'stardata'
    DB_USER = 'tu_usuario_de_mysql'
    DB_PASSWORD = 'tu_contraseña_de_mysql'
    ```

5.  **Crea las tablas en tu base de datos MySQL:**
    ```bash
    python manage.py makemigrations
    python manage.py migrate
    ```

6.  **(Opcional) Añade datos de prueba a la base de datos:**
    ```bash
    python manage.py shell
    ```
    Y dentro del shell de Django, ejecuta:
    ```python
    from api.models import AstroObject
    AstroObject.objects.create(name='Sirius (MySQL)', constellation='Canis Major', ra='06h 45m 08.9s', dec='-16° 42′ 58″')
    AstroObject.objects.create(name='Betelgeuse (MySQL)', constellation='Orion', ra='05h 55m 10.3s', dec='+07° 24′ 25″')
    quit()
    ```

7.  **¡Arranca el servidor!**
    ```bash
    python manage.py runserver 0.0.0.0:8000
    ```
    Ahora tu servidor estará escuchando. Deberías poder visitarlo en tu navegador en `http://127.0.0.1:8000/api/estrellas/`.

---

### 2. Configuración del Frontend (App Móvil)

Estos pasos prepararán la aplicación de Flutter.

1.  **Navega a la carpeta de la aplicación:**
    *(Abre una nueva terminal o usa la que ya tienes)*
    ```bash
    cd Telescopio-Goto/Software/Horizonte/app
    ```

2.  **Instala todas las dependencias de Flutter:**
    ```bash
    flutter pub get
    ```

3.  **(Solo la primera vez) Genera el ícono de la aplicación:**
    ```bash
    flutter pub run flutter_launcher_icons
    ```

4.  **Verifica que tu dispositivo esté conectado:**
    Conecta tu teléfono Android por USB (con depuración activada) o abre un emulador.
    ```bash
    flutter devices
    ```
    Deberías ver tu dispositivo en la lista.

5.  **¡Ejecuta la aplicación!**
    Asegúrate de que el servidor Django esté corriendo.
    ```bash
    flutter run
    ```
    La aplicación se instalará y se ejecutará en tu móvil.


