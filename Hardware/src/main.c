#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "pico/stdlib.h"         // Funciones estÃ¡ndar de la Pico
#include "hardware/i2c.h"        // Para el sensor MPU9250
#include "hardware/sync.h"       // Para operaciones atÃ³micas

#include "hardware/gpio.h"
#include "pico/time.h"           // Timers y 'sleep'
#include "pico/cyw43_arch.h"   // Para BLE/WiFi (necesario para BLE)
#include "lwip/netif.h"
#include "lwip/udp.h"     // <--- AÃ‘ADIR ESTE
#include "lwip/pbuf.h"
#include "pico/sync.h"
// ----------------------------
// Includes de BTstack (BLE)
// ----------------------------
#include "btstack.h"
#include "btstack_config.h"
#include "btstack_util.h"
#include "hci.h"
#include "gap.h"
#include "hardware/uart.h"


// Perfil GATT (generado por BTstack, define los servicios y caracterÃ­sticas)
#include "Horizonte.h" 

#define IMU 0 // cambiar a uno si se cuenta con un IMU con magnometro fisico

// ----------------------------
// Definiciones de Hardware
// ----------------------------
#if IMU

#define I2C_PORT i2c1           // Bus I2C a usar
#define I2C_SDA_PIN 6           // GP6
#define I2C_SCL_PIN 7           // GP7

// --- MPU9250 (AcelerÃ³metro + Giroscopio) ---
#define MPU9250_ADDR 0x68       // DirecciÃ³n I2C del MPU9250

// --- AK8963 (MagnetÃ³metro, dentro del MPU9250) ---
#define AK8963_ADDR 0x0C        // DirecciÃ³n I2C del magnetÃ³metro (visto a travÃ©s del bypass)
#define AK8963_WHO_AM_I 0x00    // Registro de identificaciÃ³n
#define AK8963_HXL 0x03         // Primer registro de datos de Mag
#define AK8963_CNTL1 0x0A       // Registro de control 1
#endif

//Motor de Azimut (Horizontal)
#define STEP_AZ_PIN 14  // Pin de Pulsos (STEP)
#define DIR_AZ_PIN  15  // Pin de DirecciÃ³n (DIR)

// Motor de Altitud (Vertical)
#define STEP_ALT_PIN 0 // Pin de Pulsos (STEP)
#define DIR_ALT_PIN  1 // Pin de DirecciÃ³n (DIR)

// (Opcional) Pin de HabilitaciÃ³n (Enable)
// Conecta ambos pines ENABLE (EN) de los drivers a este pin
// Ponerlo en BAJO (LOW) los activa.
#define DRIVER_ENABLE_PIN 18
#define STOP_COMMAND_VALUE 361.0f    // Valor imposible para Ã¡ngulos normales
#define VALID_AZIMUT_MIN 0.0f         // Azimut vÃ¡lido: 0Â° a 360Â°
#define VALID_AZIMUT_MAX 361.0f
#define VALID_ALTITUD_MIN -90.0f      // Altitud vÃ¡lida: -90Â° a +90Â°
#define VALID_ALTITUD_MAX 90.0f
// Constantes de Mapeo de Velocidad (OPTIMIZADAS)
#define VELOCITY_CONSTANT 1000000.0f  // Reducido para más velocidad
#define MIN_STEP_DELAY_US 200.0f     // Velocidad máxima aumentada
#define MAX_DELAY_US 5000.0f        
#define DEADZONE 0.5f              // Zona muerta más grande para evitar oscilación
#define PID_MAX_OUTPUT 2500.0f  
#define PID_MIN_OUTPUT 0.0f
#define LAG_COMPENSATION 0.1f    // Ajustar: 0.1 - 0.3 segundos
#define PID_MAX_INTEGRAL 150.0f   // Reducido por el lag
#define DEADBANDAZ 0.5f           // Tu precisión deseada
#define DEADBANDALT 0.1f

#if !IMU
    //puerto wifi
    #define UDP_PORT 12345
#endif

// ----------------------------
// Constantes de Filtros
// ----------------------------
#define SAMPLE_FREQ_HZ 100.0f   // Frecuencia de muestreo del IMU (100 Hz)
#define BETA 0.041f             // Ganancia del filtro Madgwick (ajustable)
#define FILTRO_ALPHA 0.1f       // Factor de suavizado para el filtro EMA (Paso Bajo)
const uint32_t sample_period_ms = (uint32_t)(1000.0f / SAMPLE_FREQ_HZ); // 10ms




// ----------------------------
// Variables de Estado Global
// ----------------------------

// --- PosiciÃ³n Objetivo (recibida de la app BLE) ---
volatile float targetAzimut = 90.0f;
volatile float targetAltitud = 45.0f;

// --- PosiciÃ³n Actual (leÃ­da del IMU y filtrada) ---
volatile float currentAzimut = 90.0f;
volatile float currentAltitud = 45.0f;     
volatile float filteredAltitud = 45.0f;  

volatile bool system_active = false;


#if !IMU

    typedef struct {
        float azimut;
        float altitud;
        volatile bool new_data; // Un "flag" para saber si hay datos frescos
    } imu_data_t;

    volatile imu_data_t phone_imu_data = {0.0f, 0.0f, false};

    static volatile uint64_t last_udp_packet_ms = 0;

#endif

#if IMU
// --- CalibraciÃ³n del Giroscopio ---
    float gyro_offset_x = 0.0f; // Error de fÃ¡brica (bias) del giroscopio en X
    float gyro_offset_y = 0.0f; // Error de fÃ¡brica (bias) del giroscopio en Y
    float gyro_offset_z = 0.0f; // Error de fÃ¡brica (bias) del giroscopio en Z
#endif

// --- Estado de BLE ---
static btstack_packet_callback_registration_t hci_event_callback_registration;
static bool notifications_enabled = false;    // Â¿La app ha pedido que le enviemos datos?
static hci_con_handle_t ble_con_handle = HCI_CON_HANDLE_INVALID; // Handle de la conexiÃ³n activa
static btstack_timer_source_t main_loop_timer; // Timer principal del bucle


// --- Estructuras de Datos ---

#if IMU
/**
 * @brief Estructura para el filtro Madgwick (cuaterniones).
 */
typedef struct {
    float q0, q1, q2, q3; // Componentes del cuaterniÃ³n
    float beta;           // Ganancia del filtro
} Madgwick_t;

Madgwick_t mad;      // Instancia del filtro Madgwick
#endif
/**
 * @brief Estructura para el controlador PID.
 */
typedef struct {
    float Kp, Ki, Kd;      // Ganancias (Constantes) del PID
    float integral;      // TÃ©rmino integral acumulado
    float prev_error;    // Error de la iteraciÃ³n anterior (para el tÃ©rmino derivativo)
    absolute_time_t last_time; // Marca de tiempo de la Ãºltima computaciÃ³n

    float prev_measurement;      // MOVIDO AQUÍ (Antes era static)
    float filtered_derivative;
} PID_t;

// --- Instancias Globales ---
PID_t pidAz, pidAlt; // Instancias de los controladores PID para cada eje

volatile uint32_t g_az_step_delay_us = 0;
volatile uint32_t g_alt_step_delay_us = 0;

static bool is_stalled_az = false;
static bool is_stalled_alt = false;
static float last_checked_az = 0.0f;
static float last_checked_alt = 0.0f;
static uint64_t stall_check_timer_az = 0;
static uint64_t stall_check_timer_alt = 0;
static volatile uint32_t drivers_active = 0; // Usamos uint32_t para operaciones atÃ³micas (0=inactivo, 1=activo)


// Margen de movimiento (en grados) para detectar un atasco
#define STALL_MOTION_THRESHOLD 0.1f  // Reducido para menos falsos positivos
// Tiempo (en ms) que esperamos un atasco antes de reaccionar
#define STALL_TIME_MS 5000           // Aumentado para más tolerancia

// ----------------------------
// Prototipos de Funciones
// ----------------------------
static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static int att_write_callback(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size);
void ble_on_write(const uint8_t *data, uint16_t len);
void ble_send_position(float yaw, float pitch);
void set_drivers_enable(bool enable);


// -----------------------------------------------------------------------------
// SECCIÃ“N 1: CONTROLADORES PID
// -----------------------------------------------------------------------------

/**
 * @brief Inicializa una estructura de controlador PID con sus ganancias.
 * @param pid Puntero a la estructura PID a inicializar.
 * @param p Ganancia Proporcional (Kp).
 * @param i Ganancia Integral (Ki).
 * @param d Ganancia Derivativa (Kd).
 */
void pid_init(PID_t *pid, float p, float i, float d) {
    pid->Kp = p;
    pid->Ki = i;
    pid->Kd = d;
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->prev_measurement = 0.0f;        // ¡IMPORTANTE!
    pid->filtered_derivative = 0.0f;     // ¡IMPORTANTE!
    pid->last_time = get_absolute_time();
}

/**
 * @brief Controla el estado del pin ENABLE de los drivers.
 * @param enable true para activar los drivers (pin LOW), false para desactivarlos (pin HIGH).
 */
void set_drivers_enable(bool enable) {
    uint32_t interrupts = save_and_disable_interrupts();
    
    printf("\nâ”â”â” set_drivers_enable(%s) â”â”â”\n", enable ? "TRUE" : "FALSE");
    printf("   Estado ANTES: drivers_active=%u, PIN=%d\n", drivers_active, gpio_get(DRIVER_ENABLE_PIN));
    
    if (enable) {
        gpio_put(DRIVER_ENABLE_PIN, 0);  // LOW activa los drivers
        drivers_active = 1;
        sleep_us(10); // Dar tiempo al hardware
        printf("   âœ… Comando ACTIVAR ejecutado\n");
    } else {
        gpio_put(DRIVER_ENABLE_PIN, 1);  // HIGH desactiva los drivers
        drivers_active = 0;
        sleep_us(10);
        printf("   âŒ Comando DESACTIVAR ejecutado\n");
    }
    
    printf("   Estado DESPUÃ‰S: drivers_active=%u, PIN=%d\n", drivers_active, gpio_get(DRIVER_ENABLE_PIN));
    printf("â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”\n\n");
    
    restore_interrupts(interrupts);
}

/**
 * @brief Normaliza un ángulo al rango [0, 360)
 * @param angle Ãngulo en grados (puede ser cualquier valor)
 * @return Ãngulo normalizado en el rango [0, 360)
 */
static inline float normalize_angle_360(float angle) {
    // Usar fmodf para reducir al rango [-360, 360]
    angle = fmodf(angle, 360.0f);
    
    // Si es negativo, hacerlo positivo
    if (angle < 0.0f) {
        angle += 360.0f;
    }
    
    return angle;
}

float ramp(PID_t *pid, float setpoint, float measurement) {
    // 1. Calcular Error
    float error = setpoint - measurement;

    // 2. Normalizar error (-180 a 180) para tomar el camino más corto
    while (error > 180.0f) error -= 360.0f;
    while (error < -180.0f) error += 360.0f;
    
    // 3. Guardar el signo (CRUCIAL para la dirección del motor)
    float sign = (error > 0) ? 1.0f : -1.0f;
    
    // 4. Trabajar con valor absoluto para calcular la magnitud de la velocidad
    float abs_error = fabs(error);
    
    // 5. Zona muerta (Si estamos muy cerca, paramos)
    if (abs_error < DEADBANDAZ) {
        return 0.0f; 
    }
    
    float speed;
    
    // 6. Lógica Escalonada (Logic Control)
    if (abs_error > 30.0f) {
        // --- LEJOS: Velocidad Máxima ---
        speed = PID_MAX_OUTPUT; 
        
    } else if (abs_error > 1.0f) {
        // --- ACERCÁNDOSE: Rampa Proporcional ---
        // Ecuación para escalar suavemente de MIN_OUTPUT a MAX_OUTPUT
        // Rango de error: 5 a 30 (Delta = 25)
        float factor = (abs_error - 5.0f) / 25.0f; 
        speed = PID_MIN_OUTPUT + (PID_MAX_OUTPUT - PID_MIN_OUTPUT) * factor;
        
    } else {
        // --- MUY CERCA: Velocidad Mínima Constante ---
        // Esto reemplaza al término Integral. Mantiene fuerza para vencer la fricción final.
        speed = PID_MIN_OUTPUT; 
    }
    
    // 7. Devolver velocidad con su dirección original
    return speed * sign;
}

 // Segundos estimados de lag (ajustar según tu sistema)

float pid_compute(PID_t *pid, float setpoint, float measurement, float deadband) {
    // ========================================
    // 1. TIEMPO
    // ========================================
    uint64_t now = to_us_since_boot(get_absolute_time());
    uint64_t dt_us = now - to_us_since_boot(pid->last_time);
    pid->last_time = get_absolute_time();

    if (dt_us == 0) return 0.0f;
    float dt = dt_us / 1000000.0f;
    if (dt > 0.1f) dt = 0.01f;

    // ========================================
    // 2. ERROR
    // ========================================
    float error = setpoint - measurement;
    while (error > 180.0f) error -= 360.0f;
    while (error < -180.0f) error += 360.0f;

    float abs_error = fabs(error);

    // ========================================
    // 3. PREDICCIÓN DE POSICIÓN (Compensar Lag)
    // ========================================
    // Estimar dónde ESTÁ AHORA el telescopio basado en velocidad anterior
    float velocity = (measurement - pid->prev_measurement) / dt;
    
    // Normalizar velocidad
    while (velocity > 180.0f) velocity -= 360.0f;
    while (velocity < -180.0f) velocity += 360.0f;
    
    // Posición predicha = medición + (velocidad × lag)
    float predicted_position = measurement + (velocity * LAG_COMPENSATION);
    
    // Recalcular error con posición predicha
    float predicted_error = setpoint - predicted_position;
    while (predicted_error > 180.0f) predicted_error -= 360.0f;
    while (predicted_error < -180.0f) predicted_error += 360.0f;

    // ========================================
    // 4. ZONA MUERTA - Usar error predicho
    // ========================================
    if (fabs(predicted_error) < deadband) {
        pid->integral = 0.0f;
        pid->prev_error = 0.0f;
        pid->prev_measurement = measurement;
        return 0.0f;
    }

    // ========================================
    // 5. PROPORCIONAL - Usar error predicho
    // ========================================
    float P = pid->Kp * predicted_error;

    // ========================================
    // 6. INTEGRAL - Con anti-windup agresivo por el lag
    // ========================================
    float I = 0.0f;
    
    if (pid->Ki > 0.0f) {
        // Solo integrar si estamos MUY cerca (porque el lag ya causa problemas)
        if (abs_error < 3.0f) {
            pid->integral += error * dt;
            
            // Límite estricto
            float max_i = PID_MAX_INTEGRAL;
            if (pid->integral > max_i) pid->integral = max_i;
            if (pid->integral < -max_i) pid->integral = -max_i;
        } else {
            // Lejos: reducir integral gradualmente (no resetear de golpe)
            pid->integral *= 0.95f;
        }
        
        // Reset si cruzamos el objetivo
        if ((error > 0 && pid->prev_error < 0) || (error < 0 && pid->prev_error > 0)) {
            pid->integral = 0.0f;
        }
        
        I = pid->Ki * pid->integral;
    }

    // ========================================
    // 7. DERIVATIVO - Crítico para compensar lag
    // ========================================
    float D = 0.0f;
    
    if (pid->Kd > 0.0f) {
        // Filtrar velocidad
        pid->filtered_derivative = 0.7f * pid->filtered_derivative + 0.3f * velocity;
        
        // D se opone al movimiento (FRENA antes de llegar)
        D = -pid->Kd * pid->filtered_derivative;
        
        // Limitar
        float D_limit = PID_MAX_OUTPUT * 0.4f;
        if (D > D_limit) D = D_limit;
        if (D < -D_limit) D = -D_limit;
    }
    
    pid->prev_measurement = measurement;
    pid->prev_error = error;

    // ========================================
    // 8. SALIDA
    // ========================================
    float output = P + I + D;

    // Saturación
    if (output > PID_MAX_OUTPUT) output = PID_MAX_OUTPUT;
    if (output < -PID_MAX_OUTPUT) output = -PID_MAX_OUTPUT;

    // Velocidad mínima
    if (output > 0.0f && output < PID_MIN_OUTPUT) {
        output = PID_MIN_OUTPUT;
    } else if (output < 0.0f && output > -PID_MIN_OUTPUT) {
        output = -PID_MIN_OUTPUT;
    }

    return output;
}

// -----------------------------------------------------------------------------
// SECCIÃ“N 2: FILTRO DE FUSIÃ“N (MADGWICK)
// -----------------------------------------------------------------------------

#if IMU
    /**
     * @brief Inicializa el filtro Madgwick.
     * @param m Puntero a la estructura Madgwick.
     * @param beta Ganancia del filtro.
     */
    void madgwick_init(Madgwick_t *m, float beta) {
        m->q0 = 1.0f; m->q1 = 0.0f; m->q2 = 0.0f; m->q3 = 0.0f;
        m->beta = beta;
    }

    /**
     * @brief Actualiza el filtro Madgwick con una nueva muestra de sensores.
     * (ImplementaciÃ³n completa del algoritmo AHRS de Madgwick)
     * @param gx, gy, gz Datos del Giroscopio (en rad/s).
     * @param ax, ay, az Datos del AcelerÃ³metro (normalizados o m/s^2).
     * @param mx, my, mz Datos del MagnetÃ³metro (normalizados o ÂµT).
     * @param sampleFreq Frecuencia de muestreo (en Hz).
     */
    void madgwick_update(Madgwick_t *m, float gx, float gy, float gz,
                           float ax, float ay, float az,
                           float mx, float my, float mz, float sampleFreq)
    {
        float q0 = m->q0, q1 = m->q1, q2 = m->q2, q3 = m->q3;
        float beta = m->beta;

        // Normalizar datos del acelerÃ³metro
        float norm = sqrtf(ax*ax + ay*ay + az*az);
        if (norm == 0.0f) return; // Evitar divisiÃ³n por cero
        ax /= norm; ay /= norm; az /= norm;

        // Normalizar datos del magnetÃ³metro (si se usan)
        bool useMag = !(mx == 0.0f && my == 0.0f && mz == 0.0f);
        if (useMag) {
            norm = sqrtf(mx*mx + my*my + mz*mz);
            if (norm == 0.0f) useMag = false; // Datos malos, no usar
            else { mx /= norm; my /= norm; mz /= norm; }
        }

        // --- Variables auxiliares ---
        float _2q0mx = 2.0f * q0 * mx;
        float _2q0my = 2.0f * q0 * my;
        float _2q0mz = 2.0f * q0 * mz;
        float _2q1mx = 2.0f * q1 * mx;
        float _2q0 = 2.0f * q0;
        float _2q1 = 2.0f * q1;
        float _2q2 = 2.0f * q2;
        float _2q3 = 2.0f * q3;
        float _2q0q2 = 2.0f * q0 * q2;
        float _2q2q3 = 2.0f * q2 * q3;
        float q0q0 = q0 * q0;
        float q0q1 = q0 * q1;
        float q0q2 = q0 * q2;
        float q0q3 = q0 * q3;
        float q1q1 = q1 * q1;
        float q1q2 = q1 * q2;
        float q1q3 = q1 * q3;
        float q2q2 = q2 * q2;
        float q2q3 = q2 * q3;
        float q3q3 = q3 * q3;

        // --- CÃ¡lculo del gradiente (tÃ©rmino de error) ---
        float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
        if (useMag) {
            // CÃ¡lculo completo de 9 ejes (Giro + Accel + Mag)
            float hx = mx * q0q0 - _2q0my * q3 + _2q0mz * q2 + mx * q1q1 + _2q1 * my * q2 + _2q1 * mz * q3 - mx * q2q2 - mx * q3q3;
            float hy = _2q0mx * q3 + my * q0q0 - _2q0mz * q1 + _2q1mx * q2 - my * q1q1 + my * q2q2 + _2q2 * mz * q3 - my * q3q3;
            float _2bx = sqrtf(hx * hx + hy * hy);
            float _2bz = -_2q0mx * q2 + _2q0my * q1 + mz * q0q0 + _2q1mx * q3 - mz * q1q1 + _2q2 * my * q3 - mz * q2q2 + mz * q3q3;
            float _4bx = 2.0f * _2bx;
            float _4bz = 2.0f * _2bz;

            s0 = -_2q2 * (2.0f*(q1q3 - q0q2) - ax) + _2q1 * (2.0f*(q0q1 + q2q3) - ay) - _2bz * q2 * (_2bx*(0.5f - q2q2 - q3q3) + _2bz*(q1q3 - q0q2) - mx) + (-_2bx * q3 + _2bz * q1) * (_2bx*(q1q2 - q0q3) + _2bz*(q0q1 + q2q3) - my) + _2bx * q2 * (_2bx*(q0q2 + q1q3) + _2bz*(0.5f - q1q1 - q2q2) - mz);
            s1 = _2q3 * (2.0f*(q1q3 - q0q2) - ax) + _2q0 * (2.0f*(q0q1 + q2q3) - ay) - 4.0f * q1 * (2.0f*(0.5f - q1q1 - q2q2) - az) + _2bz * q3 * (_2bx*(0.5f - q2q2 - q3q3) + _2bz*(q1q3 - q0q2) - mx) + (_2bx * q2 + _2bz * q0) * (_2bx*(q1q2 - q0q3) + _2bz*(q0q1 + q2q3) - my) + (_2bx * q3 - _4bz * q1) * (_2bx*(q0q2 + q1q3) + _2bz*(0.5f - q1q1 - q2q2) - mz);
            s2 = -_2q0 * (2.0f*(q1q3 - q0q2) - ax) + _2q3 * (2.0f*(q0q1 + q2q3) - ay) - 4.0f * q2 * (2.0f*(0.5f - q1q1 - q2q2) - az) + (-_4bx * q2 - _2bz * q0) * (_2bx*(0.5f - q2q2 - q3q3) + _2bz*(q1q3 - q0q2) - mx) + (_2bx * q1 + _2bz * q3) * (_2bx*(q1q2 - q0q3) + _2bz*(q0q1 + q2q3) - my) + (_2bx * q0 - _4bz * q2) * (_2bx*(q0q2 + q1q3) + _2bz*(0.5f - q1q1 - q2q2) - mz);
            s3 = _2q1 * (2.0f*(q1q3 - q0q2) - ax) + _2q2 * (2.0f*(q0q1 + q2q3) - ay) + (-_4bx * q3 + _2bz * q1) * (_2bx*(0.5f - q2q2 - q3q3) + _2bz*(q1q3 - q0q2) - mx) + (-_2bx * q0 + _2bz * q2) * (_2bx*(q1q2 - q0q3) + _2bz*(q0q1 + q2q3) - my) + _2bx * q1 * (_2bx*(q0q2 + q1q3) + _2bz*(0.5f - q1q1 - q2q2) - mz);
        } else {
            // CÃ¡lculo de 6 ejes (Giro + Accel) si no hay magnetÃ³metro
            float _2q0q1 = 2.0f * q0 * q1;
            float _2q0q2 = 2.0f * q0 * q2;
            float _2q1q3 = 2.0f * q1 * q3;
            float _2q2q3 = 2.0f * q2 * q3;
            s0 = _2q2 * (2.0f*(q1q3 - q0q2) - ax) + _2q1 * (2.0f*(q0q1 + q2q3) - ay);
            s1 = _2q3 * (2.0f*(q1q3 - q0q2) - ax) + 4.0f * q1 * (2.0f*(0.5f - q1q1 - q2q2) - az) - _2q0 * (2.0f*(q0q1 + q2q3) - ay);
            s2 = -_2q0 * (2.0f*(q1q3 - q0q2) - ax) + 4.0f * q2 * (2.0f*(0.5f - q1q1 - q2q2) - az) - _2q3 * (2.0f*(q0q1 + q2q3) - ay);
            s3 = _2q1 * (2.0f*(q1q3 - q0q2) - ax) - _2q2 * (2.0f*(q0q1 + q2q3) - ay);
        }

        // Normalizar magnitud del paso
        // ** CORRECCIÃ“N DE BUG **: Se eliminÃ³ una lÃ­nea duplicada que causaba divisiÃ³n por cero.
        float s_norm = sqrtf(s0*s0 + s1*s1 + s2*s2 + s3*s3);
        if (s_norm > 0.0f) {
            s0 /= s_norm; s1 /= s_norm; s2 /= s_norm; s3 /= s_norm;
        } else {
            s0 = 0.0f; s1 = 0.0f; s2 = 0.0f; s3 = 0.0f;
        }

        // Integrar la tasa de cambio del cuaterniÃ³n
        // (Esta parte integra el giroscopio y aplica la correcciÃ³n)
        float qDot0 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz) - beta * s0;
        float qDot1 = 0.5f * ( q0 * gx + q2 * gz - q3 * gy) - beta * s1;
        float qDot2 = 0.5f * ( q0 * gy - q1 * gz + q3 * gx) - beta * s2;
        float qDot3 = 0.5f * ( q0 * gz + q1 * gy - q2 * gx) - beta * s3;

        // Integrar para obtener el nuevo cuaterniÃ³n
        q0 += qDot0 * (1.0f / sampleFreq);
        q1 += qDot1 * (1.0f / sampleFreq);
        q2 += qDot2 * (1.0f / sampleFreq);
        q3 += qDot3 * (1.0f / sampleFreq);

        // Normalizar el cuaterniÃ³n
        float qnorm = sqrtf(q0*q0 + q1*q1 + q2*q2 + q3*q3);
        q0 /= qnorm; q1 /= qnorm; q2 /= qnorm; q3 /= qnorm;

        // Guardar el estado
        m->q0 = q0; m->q1 = q1; m->q2 = q2; m->q3 = q3;
    }

    /**
     * @brief Convierte el cuaterniÃ³n del filtro en Ã¡ngulos de Euler (Yaw, Pitch, Roll).
     * @param m Puntero a la estructura Madgwick.
     * @param yaw Puntero para guardar el Azimut (en grados).
     * @param pitch Puntero para guardar la Altitud (en grados).
     * @param roll Puntero para guardar el Roll (en grados).
     */
    void madgwick_get_euler(Madgwick_t *m, float *yaw, float *pitch, float *roll) {
        float q0 = m->q0, q1 = m->q1, q2 = m->q2, q3 = m->q3;
        float ysqr = q2 * q2;

        float t0 = +2.0f * (q0 * q1 + q2 * q3);
        float t1 = +1.0f - 2.0f * (q1 * q1 + ysqr);
        float roll_r = atan2f(t0, t1);

        float t2 = +2.0f * (q0 * q2 - q3 * q1);
        t2 = (t2 > 1.0f) ? 1.0f : t2; // Limitar a +/- 1.0
        t2 = (t2 < -1.0f) ? -1.0f : t2;
        float pitch_r = asinf(t2);

        float t3 = +2.0f * (q0 * q3 + q1 * q2);
        float t4 = +1.0f - 2.0f * (ysqr + q3 * q3);
        float yaw_r = atan2f(t3, t4);

        // Convertir de radianes a grados
        *yaw = yaw_r * (180.0f / M_PI);
        *pitch = pitch_r * (180.0f / M_PI);
        *roll = roll_r * (180.0f / M_PI);
    }


    // -----------------------------------------------------------------------------
    // SECCIÃ“N 3: DRIVERS DE SENSORES (I2C)
    // -----------------------------------------------------------------------------

    /**
     * @brief Escribe un solo byte en un registro del MPU9250.
     */
    static bool mpu_write_reg(uint8_t reg, uint8_t val) {
        uint8_t buf[2] = {reg, val};
        int ret = i2c_write_blocking(I2C_PORT, MPU9250_ADDR, buf, 2, false);
        return (ret == 2);
    }

    /**
     * @brief Lee mÃºltiples bytes desde un registro del MPU9250.
     */
    static bool mpu_read_regs(uint8_t reg, uint8_t *dst, size_t len) {
        int ret = i2c_write_blocking(I2C_PORT, MPU9250_ADDR, &reg, 1, true); // true = "keep bus active"
        if (ret != 1) return false;
        ret = i2c_read_blocking(I2C_PORT, MPU9250_ADDR, dst, len, false);
        return (ret == (int)len);
    }

    /**
     * @brief Inicializa el MPU9250 (Giro + Accel) y activa el modo Bypass I2C.
     * El modo Bypass es CRÃTICO para permitir que el Pico hable
     * directamente con el magnetÃ³metro AK8963.
     */
    static bool mpu_init() {
        // Resetear el dispositivo
        if (!mpu_write_reg(0x6B, 0x80)) return false; // PWR_MGMT_1: Reset
        sleep_ms(100);

        // Despertar y seleccionar fuente de reloj (PLL)
        if (!mpu_write_reg(0x6B, 0x01)) return false; // PWR_MGMT_1: Clock = PLL
        sleep_ms(50);

        // Deshabilitar el I2C Master interno del MPU (crucial para el bypass)
        if (!mpu_write_reg(0x6A, 0x00)) { // USER_CTRL: I2C_MST_EN = 0
            printf("âŒ FALLO al deshabilitar I2C master\n");
            return false;
        }
        sleep_ms(10);

        // Habilitar el modo I2C Bypass
        // INT_PIN_CFG (0x37): bit 1 (I2C_BYPASS_EN) = 1
        if (!mpu_write_reg(0x37, 0x02)) {
            printf("âŒ FALLO al habilitar bypass\n");
            return false;
        }
        sleep_ms(10);

        // Verificar que el modo bypass estÃ© activo
        uint8_t bypass_check;
        if (!mpu_read_regs(0x37, &bypass_check, 1)) {
            printf("âŒ FALLO al leer INT_PIN_CFG\n");
            return false;
        }
        printf("    INT_PIN_CFG = 0x%02X (bypass %s)\n", 
               bypass_check, 
               (bypass_check & 0x02) ? "HABILITADO âœ…" : "DESHABILITADO âŒ");
        
        // Dar tiempo extra para que el bus se estabilice
        sleep_ms(100);
        
        return true;
    }

    /**
     * @brief Lee los datos crudos de AcelerÃ³metro y Giroscopio.
     * @param ax, ay, az Punteros para guardar datos de Accel (en m/s^2).
     * @param gx, gy, gz Punteros para guardar datos de Giro (en rad/s).
     */
    static bool mpu_read_accel_gyro(float *ax, float *ay, float *az, float *gx, float *gy, float *gz) {
        uint8_t buf[14];
        // Leer 14 bytes (Accel, Temp, Gyro) en una sola transacciÃ³n
        if (!mpu_read_regs(0x3B, buf, 14)) return false;

        // Combinar bytes (Big-Endian)
        int16_t raw_ax = (buf[0] << 8) | buf[1];
        int16_t raw_ay = (buf[2] << 8) | buf[3];
        int16_t raw_az = (buf[4] << 8) | buf[5];
        // int16_t raw_temp = (buf[6] << 8) | buf[7]; // (Temperatura no usada)
        int16_t raw_gx = (buf[8] << 8) | buf[9];
        int16_t raw_gy = (buf[10] << 8) | buf[11];
        int16_t raw_gz = (buf[12] << 8) | buf[13];

        // Convertir valores crudos a unidades del SI
        // Sensibilidad (por defecto):
        // Accel: +/-2g  => 16384 LSB/g  (g = 9.80665 m/s^2)
        // Gyro:  +/-250 deg/s => 131 LSB/(deg/s) (deg/s * (PI/180) = rad/s)

        *ax = (float)raw_ax / 16384.0f * 9.80665f;
        *ay = (float)raw_ay / 16384.0f * 9.80665f;
        *az = (float)raw_az / 16384.0f * 9.80665f;

        *gx = (float)raw_gx / 131.0f * (M_PI / 180.0f);
        *gy = (float)raw_gy / 131.0f * (M_PI / 180.0f);
        *gz = (float)raw_gz / 131.0f * (M_PI / 180.0f);

        return true;
    }

    /**
     * @brief Inicializa el magnetÃ³metro AK8963.
     * Esta funciÃ³n es compleja debido a la naturaleza quisquillosa del chip.
     * Incluye reintentos y verificaciones.
     */
    bool ak8963_init() {
        printf("ðŸ”§ Inicializando MagnetÃ³metro AK8963...\n");
        sleep_ms(100); // Dar tiempo al bus

        uint8_t who_am_i = 0;

        // PASO 1: Intentar leer el WHO_AM_I (0x00)
        uint8_t reg = AK8963_WHO_AM_I;
        int ret = i2c_write_blocking(I2C_PORT, AK8963_ADDR, &reg, 1, true);
        printf("   i2c_write_blocking (WHO_AM_I) retornÃ³: %d\n", ret);

        if (ret != 1) {
            printf("âŒ FALLO: AK8963 no acepta comandos (Verificar bypass)\n");
            return false;
        }

        sleep_ms(1);

        ret = i2c_read_blocking(I2C_PORT, AK8963_ADDR, &who_am_i, 1, false);
        printf("   i2c_read_blocking retornÃ³: %d, WHO_AM_I = 0x%02X (esperado: 0x48)\n", ret, who_am_i);

        if (ret != 1 || who_am_i != 0x48) {
            printf("âŒ MagnetÃ³metro no identificado correctamente\n");
            return false;
        }
        printf("   âœ… WHO_AM_I correcto!\n");

        // PASO 2: Poner en Power-Down antes de configurar
        uint8_t power_down[2] = {AK8963_CNTL1, 0x00};
        i2c_write_blocking(I2C_PORT, AK8963_ADDR, power_down, 2, false);
        sleep_ms(10);

        // PASO 3: Configurar modo continuo 100Hz, 16-bit
        uint8_t mode_cmd[2] = {AK8963_CNTL1, 0x16}; // 0b00010110 = 16-bit, 100Hz
        ret = i2c_write_blocking(I2C_PORT, AK8963_ADDR, mode_cmd, 2, false);
        printf("   Configurar modo: ret=%d\n", ret);

        if (ret != 2) {
            printf("âŒ FALLO al configurar modo de mediciÃ³n\n");
            return false;
        }
        sleep_ms(10);

        printf("âœ… MagnetÃ³metro AK8963 inicializado\n");
        return true;
    }

    /**
     * @brief Lee los datos crudos del MagnetÃ³metro (AK8963).
     * @param mx, my, mz Punteros para guardar datos del Mag (en ÂµT).
     */
    bool mpu_read_mag(float *mx, float *my, float *mz) {
        // PASO 1: Verificar si hay datos nuevos (Registro ST1)
        uint8_t st1;
        uint8_t reg = 0x02; // ST1 register
        if (i2c_write_blocking(I2C_PORT, AK8963_ADDR, &reg, 1, true) != 1) return false;
        if (i2c_read_blocking(I2C_PORT, AK8963_ADDR, &st1, 1, false) != 1) return false;

        // Bit 0 (DRDY) debe ser 1
        if (!(st1 & 0x01)) {
            return false; // No hay datos nuevos
        }

        // PASO 2: Leer los 7 bytes de datos (HXL a ST2)
        uint8_t buf[7];
        reg = AK8963_HXL; // Registro de inicio (0x03)
        if (i2c_write_blocking(I2C_PORT, AK8963_ADDR, &reg, 1, true) != 1) return false;
        if (i2c_read_blocking(I2C_PORT, AK8963_ADDR, buf, 7, false) != 7) return false;

        // PASO 3: Verificar overflow (bit 3 de ST2)
        if (buf[6] & 0x08) {
            printf("âš ï¸  Overflow en magnetÃ³metro\n");
            return false;
        }

        // PASO 4: Combinar bytes (Little-Endian)
        int16_t raw_mx = (int16_t)((buf[1] << 8) | buf[0]);
        int16_t raw_my = (int16_t)((buf[3] << 8) | buf[2]);
        int16_t raw_mz = (int16_t)((buf[5] << 8) | buf[4]);

        // Convertir a microTeslas (ÂµT)
        // Sensibilidad para 16-bit: 0.15 ÂµT/LSB
        *mx = (float)raw_mx * 0.15f;
        *my = (float)raw_my * 0.15f;
        *mz = (float)raw_mz * 0.15f;

        return true;
    }

    /**
    * @brief Calibra el giroscopio al inicio.
    * Mide el error de fÃ¡brica (offset) mientras el dispositivo
    * estÃ¡ quieto y lo guarda para restarlo despuÃ©s.
    * @param num_muestras NÃºmero de lecturas para promediar.
    */
    void calibrar_giroscopio(int num_muestras) {
        printf("ðŸ”§ Calibrando Giroscopio... Â¡No mover el dispositivo!\n");

        float sum_gx = 0.0f, sum_gy = 0.0f, sum_gz = 0.0f;
        float ax, ay, az, gx, gy, gz; // Variables temporales

        for (int i = 0; i < num_muestras; i++) {
            if (mpu_read_accel_gyro(&ax, &ay, &az, &gx, &gy, &gz)) {
                sum_gx += gx;
                sum_gy += gy;
                sum_gz += gz;
            }
            sleep_ms(5); // PequeÃ±a pausa
        }

        // Calcular el promedio del error (offset)
        gyro_offset_x = sum_gx / (float)num_muestras;
        gyro_offset_y = sum_gy / (float)num_muestras;
        gyro_offset_z = sum_gz / (float)num_muestras;

        printf("âœ… CalibraciÃ³n completa.\n");
        printf("   Offset Gx: %f rad/s\n", gyro_offset_x);
        printf("   Offset Gy: %f rad/s\n", gyro_offset_y);
        printf("   Offset Gz: %f rad/s\n", gyro_offset_z);
    }

    /**
     * @brief FunciÃ³n de diagnÃ³stico I2C para escanear el bus.
     */
    void diagnostico_i2c() {
        printf("\nðŸ” === DIAGNÃ“STICO I2C DETALLADO ===\n");

        uint8_t addrs_esperadas[] = {0x68, 0x69, 0x0C};
        const char* nombres[] = {"MPU9250 (0x68)", "MPU9250 Alt (0x69)", "AK8963 (0x0C)"};

        for (int i = 0; i < 3; i++) {
            uint8_t dummy = 0;
            int ret = i2c_write_blocking(I2C_PORT, addrs_esperadas[i], &dummy, 0, false);
            printf("   Probando %s: %s (ret=%d)\n", 
                   nombres[i], 
                   ret >= 0 ? "âœ… RESPONDE" : "âŒ NO RESPONDE",
                   ret);
        }

        // Probar lectura real del WHO_AM_I del MPU
        uint8_t who_am_i_reg = 0x75;
        uint8_t who_am_i_val = 0;

        printf("\nðŸ“– Intentando leer WHO_AM_I del MPU9250...\n");
        int ret1 = i2c_write_blocking(I2C_PORT, MPU9250_ADDR, &who_am_i_reg, 1, true);
        printf("   Write: %s (ret=%d)\n", ret1 == 1 ? "âœ… OK" : "âŒ FALLO", ret1);

        if (ret1 == 1) {
            int ret2 = i2c_read_blocking(I2C_PORT, MPU9250_ADDR, &who_am_i_val, 1, false);
            printf("   Read: %s (ret=%d, valor=0x%02X)\n", 
                   ret2 == 1 ? "âœ… OK" : "âŒ FALLO", 
                   ret2, 
                   who_am_i_val);
        }
    }
#endif



// -----------------------------------------------------------------------------
// SECCIÃ“N 5: COMUNICACIÃ“N BLUETOOTH (BTstack)
// -----------------------------------------------------------------------------

/**
 * @brief Callback para manejar escrituras en el servidor GATT (BLE).
 * Se llama cuando la app de Flutter escribe datos en el Pico.
 */
static int att_write_callback(hci_con_handle_t connection_handle, uint16_t att_handle,
                              uint16_t transaction_mode, uint16_t offset,
                              uint8_t *buffer, uint16_t buffer_size) {
    
    printf("\nâ–¼â–¼â–¼ ATT_WRITE_CALLBACK â–¼â–¼â–¼\n");
    printf("   con_handle: 0x%04X\n", connection_handle);
    printf("   att_handle: 0x%04X\n", att_handle);
    printf("   buffer_size: %u\n", buffer_size);
    printf("   FFE2 VALUE HANDLE: 0x%04X\n", ATT_CHARACTERISTIC_0000FFE2_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE);
    printf("   FFE2 CONFIG HANDLE: 0x%04X\n", ATT_CHARACTERISTIC_0000FFE2_0000_1000_8000_00805F9B34FB_01_CLIENT_CONFIGURATION_HANDLE);
    
    // "Robar" el handle de conexiÃ³n la primera vez que se escriba algo.
    // Esto es un "workaround" en caso de que ATT_EVENT_CONNECTED falle.
    if (ble_con_handle == HCI_CON_HANDLE_INVALID) {
        printf("   â†’ Handle 'robado' de att_write_callback: 0x%04X\n", connection_handle);
        ble_con_handle = connection_handle;
    }

    // 1. Â¿La app escribiÃ³ en nuestra caracterÃ­stica de "recibir datos" (FFE2)?
    if (att_handle == ATT_CHARACTERISTIC_0000FFE2_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE) {
        printf("   âœ… MATCH: Escritura en FFE2 VALUE - llamando a ble_on_write()\n");
        ble_on_write(buffer, buffer_size); // Procesar los datos
        printf("â–²â–²â–² ATT_WRITE_CALLBACK FIN â–²â–²â–²\n\n");
        return 0;
    }
    
    // 2. Â¿La app habilitÃ³/deshabilitÃ³ las notificaciones?
    // (Esto ocurre cuando la app llama a `setNotifyValue(true)`)
    if (att_handle == ATT_CHARACTERISTIC_0000FFE2_0000_1000_8000_00805F9B34FB_01_CLIENT_CONFIGURATION_HANDLE) {
        printf("   âœ… MATCH: Escritura en FFE2 CLIENT CONFIG\n");
        if (buffer_size >= 2) {
            uint16_t config = little_endian_read_16(buffer, 0);
            notifications_enabled = (config & 0x0001) != 0; // 0x0001 = Notificaciones Habilitadas
            
            if (notifications_enabled) {
                printf("      â†’ Cliente HABILITÃ“ notificaciones en FFE2\n");
            } else {
                printf("      â†’ Cliente DESHABILITÃ“ notificaciones en FFE2\n");
            }
        }
        printf("â–²â–²â–² ATT_WRITE_CALLBACK FIN â–²â–²â–²\n\n");
        return 0;
    }
    
    printf("   âš ï¸  Handle NO RECONOCIDO - ignorando\n");
    printf("â–²â–²â–² ATT_WRITE_CALLBACK FIN â–²â–²â–²\n\n");
    return 0;
}

/**
 * @brief Procesa los datos recibidos de la app (las coordenadas objetivo).
 * @param data Buffer con los datos.
 * @param len Longitud de los datos.
 */
void ble_on_write(const uint8_t *data, uint16_t len) {
    // --- Log visual para depuración ---
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║  BLE_ON_WRITE RECIBIDO                 ║\n");
    printf("╚════════════════════════════════════════╝\n");

    // --- 1. Preprocesar datos recibidos ---
    char tmp[64];
    if (len >= sizeof(tmp)) {
        printf("   ❌ ERROR: Datos demasiado largos (%u >= %u)\n", len, (uint16_t)sizeof(tmp));
        return;
    }
    
    memcpy(tmp, data, len);
    tmp[len] = '\0'; // Asegurar terminación en null
    
    printf("   Datos: \"%s\"\n", tmp);
    
    float az = 0.0f, alt = 0.0f;
    int parsed = sscanf(tmp, "%f,%f", &az, &alt);
    
    if (parsed == 2) {
        // --- 2. Verificar COMANDO STOP ---
        // Si recibimos 361.0 (STOP_COMMAND_VALUE), detenemos todo.
        if (az == STOP_COMMAND_VALUE || alt == STOP_COMMAND_VALUE) {
            printf("\n🛑 === COMANDO STOP ===\n");
            
            // A) Apagar lógica de control
            system_active = false; 
            
            // B) Detener motores físicamente
            g_az_step_delay_us = 0;
            g_alt_step_delay_us = 0;
            set_drivers_enable(false);
 
            // C) Limpiar estado interno
            pid_init(&pidAz, pidAz.Kp, pidAz.Ki, pidAz.Kd);
            pid_init(&pidAlt, pidAlt.Kp, pidAlt.Ki, pidAlt.Kd);
            
            is_stalled_az = false;
            is_stalled_alt = false;
            stall_check_timer_az = 0;
            stall_check_timer_alt = 0;
            
            printf("✅ Sistema detenido y reiniciado.\n\n");
            return;
        }
        
        // --- 3. Procesar NUEVAS COORDENADAS ---
        printf("   ✅ Coordenadas válidas: Az=%.2f, Alt=%.2f\n", az, alt);
        
        // Si el sistema estaba en PAUSA/STOP, lo reactivamos ahora
        if (!system_active) {
            printf("🚀 REACTIVANDO SISTEMA (Inicio de Seguimiento)\n");
            
            // A) Resetear PIDs para empezar limpios (sin error acumulado viejo)
            pid_init(&pidAz, pidAz.Kp, pidAz.Ki, pidAz.Kd); 
            pid_init(&pidAlt, pidAlt.Kp, pidAlt.Ki, pidAlt.Kd);
            
            // B) Encender hardware
            if (!drivers_active) {
                set_drivers_enable(true);
            }
            
            // C) Levantar bandera maestra
            system_active = true; 
        }
        
        // --- 4. Actualizar Targets ---
        // Normalizar Azimut objetivo (0-360) por seguridad
        while (az < 0) az += 360.0f;
        while (az >= 360.0f) az -= 360.0f;

        targetAzimut = az;
        targetAltitud = alt;
        
        printf("   🎯 Nuevo Objetivo: Az=%.2f, Alt=%.2f\n", targetAzimut, targetAltitud);
        printf("════════════════════════════════════════\n\n");

    } else {
        printf("   ❌ ERROR DE FORMATO: Se esperaba 'float,float'\n");
        // Opcional: Si llegan datos basura, ¿deberíamos parar? 
        // Por seguridad, a veces es mejor ignorar o parar.
        // set_drivers_enable(false); 
    }
}

/**
 * @brief EnvÃ­a la posiciÃ³n actual (Yaw, Pitch) a la app vÃ­a notificaciÃ³n BLE.
 * @param yaw Azimut actual.
 * @param pitch Altitud actual.
 */
void ble_send_position(float yaw, float pitch) {
    // GUARDIA 1: Â¿Estamos conectados?
    if (ble_con_handle == HCI_CON_HANDLE_INVALID) {
        return;
    }
    
    // GUARDIA 2: Â¿La app nos pidiÃ³ que enviemos datos?
    if (!notifications_enabled) {
        return;
    }

    char buf[32];
    // Formatear el string "Yaw,Pitch\n"
    int n = snprintf(buf, sizeof(buf), "%.2f,%.2f\n", yaw, pitch);
    
    printf("ðŸ“¤ Enviando: %s", buf); // buf ya incluye \n
    
    // Enviar la notificaciÃ³n
    int result = att_server_notify(
        ble_con_handle, 
        ATT_CHARACTERISTIC_0000FFE2_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE, 
        (uint8_t*)buf, 
        n
    );
    
    if (result != 0) {
        printf("âŒ Error al enviar notify: %d\n", result);
    }
}

/**
 * @brief Manejador principal de eventos de BTstack.
 * Gestiona los eventos de conexiÃ³n, desconexiÃ³n y estado.
 */
static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    if (packet_type != HCI_EVENT_PACKET) return; // Solo nos interesan eventos

    //printf("DEBUG: LlegÃ³ evento HCI: 0x%02X\n", hci_event_packet_get_type(packet));

    switch (hci_event_packet_get_type(packet)) {

        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf(">>> EVENTO: BTSTACK_EVENT_STATE (HCI_STATE_WORKING)\n");
                gap_advertisements_enable(1); // Empezar a anunciarse
            }
            break;

        case HCI_EVENT_LE_META:
            // Evento "contenedor" para sub-eventos de BLE
            //printf(">>> EVENTO: HCI_EVENT_LE_META (0x3E)\n");
            switch (hci_event_le_meta_get_subevent_code(packet)) {
                
                case HCI_SUBEVENT_LE_CONNECTION_COMPLETE: {
                    uint16_t status = hci_subevent_le_connection_complete_get_status(packet);
                    if (status == 0) {
                        printf("   -> SUB-EVENTO: ConexiÃ³n fÃ­sica (HCI) exitosa.\n");
                    } else {
                        printf("   -> SUB-EVENTO: ConexiÃ³n fÃ­sica (HCI) FALLÃ“. Status: 0x%02X\n", status);
                    }
                    break;
                }
                default:
                    //printf("   -> SUB-EVENTO LE no manejado: 0x%02X\n", hci_event_le_meta_get_subevent_code(packet));
                    break;
            }
            break; 

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            printf(">>> EVENTO: HCI_EVENT_DISCONNECTION_COMPLETE (0x05)\n");
            ble_con_handle = HCI_CON_HANDLE_INVALID; // Marcar como desconectado
            notifications_enabled = false;           // Resetear notificaciones
            system_active = false;
            gap_advertisements_enable(1);            // Volver a anunciarse
            if (drivers_active) {
                set_drivers_enable(false);
                printf("ðŸ’¤ DRIVERS DESACTIVADOS por desconexiÃ³n BLE.\n");
            }
            break;
            
        case ATT_EVENT_CONNECTED:
            // Â¡Ã‰XITO! La app y el Pico han negociado servicios
            ble_con_handle = att_event_connected_get_handle(packet);
            notifications_enabled = false;
            printf("\nÂ¡Ã‰XITO! >>> EVENTO: ATT_EVENT_CONNECTED (Handle: 0x%04X)\n\n", ble_con_handle);
            set_drivers_enable(true);
            break;

        case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
            printf(">>> EVENTO: ATT_EVENT_MTU_EXCHANGE_COMPLETE\n");
            break;
        
        // Ignorar eventos ruidosos de control de flujo
        case HCI_EVENT_NUMBER_OF_COMPLETED_PACKETS: // 0x13
        case 0x6E: // Evento desconocido
            break;

        default:
            printf(">>> EVENTO: DEFAULT (No manejado): 0x%02X\n", hci_event_packet_get_type(packet));
            
            break;
    }
}

#if !IMU
    //IMU Wifi
    /**
     * @brief Callback que se ejecuta cuando llega un paquete UDP.
     */
    static void udp_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                                  const ip_addr_t *addr, u16_t port) {

        if (p != NULL) {
            // Copiar los datos del paquete a un buffer local
            char buffer[128];
            int len = (p->len > sizeof(buffer) - 1) ? sizeof(buffer) - 1 : p->len;
            memcpy(buffer, p->payload, len);
            buffer[len] = '\0'; // Asegurar terminaciÃ³n en null

            // Parsear el string "yaw,pitch" (o "az,alt")
            float az = 0.0f, alt = 0.0f;
            if (sscanf(buffer, "%f,%f", &az, &alt) == 2) {

                // Actualizar los datos globales
                phone_imu_data.azimut = az;
                phone_imu_data.altitud = -alt;
                phone_imu_data.new_data = true; // Â¡Avisar al bucle principal!

                // Opcional: imprimir para debug
                // printf("UDP Recibido: Az=%.2f, Alt=%.2f\n", az, alt);
            }

            // Â¡Importante! Liberar la memoria del paquete
            pbuf_free(p);
        }
    }
#endif

// -----------------------------------------------------------------------------
// SECCIÃ“N 6: BUCLE PRINCIPAL 
// -----------------------------------------------------------------------------

/**
 * @brief Bucle principal del programa, llamado por el timer de BTstack.
 * Se ejecuta a 100 Hz (cada 10 ms).
 */
#if IMU
    static void periodic_timer_handler(btstack_timer_source_t *ts) {
        static uint32_t loop_count = 0;
        static uint64_t last_debug_ms = 0;
        static uint64_t last_notify_ms = 0;
        loop_count++;

        uint64_t now_ms = to_ms_since_boot(get_absolute_time());

        // --------------------------------
        // 1. LEER SENSORES
        // --------------------------------
        float ax=0, ay=0, az=0, gx_raw=0, gy_raw=0, gz_raw=0;
        bool have_mpu = mpu_read_accel_gyro(&ax, &ay, &az, &gx_raw, &gy_raw, &gz_raw);

        float mx=0, my=0, mz=0;
        bool have_mag = mpu_read_mag(&mx, &my, &mz);

        // --------------------------------
        // 2. PROCESAR Y FILTRAR DATOS
        // --------------------------------
        if (have_mpu && have_mag) {

            // --- Aplicar calibraciÃ³n del giroscopio ---
            float gx_corrected = gx_raw - gyro_offset_x;
            float gy_corrected = gy_raw - gyro_offset_y;
            float gz_corrected = gz_raw - gyro_offset_z;

            // --- Actualizar el filtro de fusiÃ³n (Madgwick) ---
            madgwick_update(&mad, gx_corrected, gy_corrected, gz_corrected, 
                              ax, ay, az, 
                              mx, my, mz, 
                              SAMPLE_FREQ_HZ);
            
            // --- Obtener Ã¡ngulos de Euler ---
            float yaw, pitch, roll;
            madgwick_get_euler(&mad, &yaw, &pitch, &roll);
            
            // Guardar Yaw (Azimut) con normalizaciÃ³n completa
            currentAzimut = normalize_angle_360(yaw);
            
            // Guardar Pitch (Altitud)
            currentAltitud = pitch;

            // --- Aplicar filtro de suavizado (EMA) a la altitud ---
            filteredAltitud = (FILTRO_ALPHA * currentAltitud) + ((1.0f - FILTRO_ALPHA) * filteredAltitud);


            // -----------------------------------------------------
            // 3. CALCULAR Y EJECUTAR CONTROL (PID)
            // -----------------------------------------------------
            
            // Calcular la salida de los PID
            float outAz = pid_compute(&pidAz, targetAzimut, currentAzimut);
            float outAlt = pid_compute(&pidAlt, targetAltitud, filteredAltitud); // Usar la altitud suavizada
            
            // ---- LÃ³gica de Control de Azimut ----
            if (is_stalled_az) {
                g_az_step_delay_us = 0; // Mantener detenido si hay atasco
            } else {
                gpio_put(DIR_AZ_PIN, (outAz > 0)); // Definir DirecciÃ³n
                
                if (fabs(outAz) < DEADZONE) { 
                    g_az_step_delay_us = 0; // 0 = Detenido (Zona muerta)
                } else {
                    uint32_t delay = (uint32_t)(VELOCITY_CONSTANT / fabs(outAz));
                    
                    // Aplicar lÃ­mites superior e inferior
                    if (delay > MAX_DELAY_US) delay = MAX_DELAY_US;
                    if (delay < MIN_STEP_DELAY_US) delay = MIN_STEP_DELAY_US;
                    
                    g_az_step_delay_us = delay;
                }
            }

            // ---- LÃ³gica de Control de Altitud ----
            if (is_stalled_alt) {
                g_alt_step_delay_us = 0; // Mantener detenido si hay atasco
            } else {
                gpio_put(DIR_ALT_PIN, (outAlt > 0)); // Definir DirecciÃ³n
                
                if (fabs(outAlt) < DEADZONE) { 
                    g_alt_step_delay_us = 0; // 0 = Detenido (Zona muerta)
                } else {
                    uint32_t delay = (uint32_t)(VELOCITY_CONSTANT / fabs(outAlt));
                    
                    // Aplicar lÃ­mites superior e inferior
                    if (delay > MAX_DELAY_US) delay = MAX_DELAY_US;
                    if (delay < MIN_STEP_DELAY_US) delay = MIN_STEP_DELAY_US;
                    
                    g_alt_step_delay_us = delay;
                }
            }

            // -----------------------------------------------------
            // 4. Â¡NUEVO! DETECCIÃ“N DE ATASCO (STALL DETECTION)
            // -----------------------------------------------------

            // --- Revisar Azimut ---
            if (g_az_step_delay_us > 0) { // 1. El PID quiere moverse...
                if (stall_check_timer_az == 0) {
                    // 2. ...acaba de empezar. Iniciar temporizador y guardar posiciÃ³n.
                    stall_check_timer_az = now_ms;
                    last_checked_az = currentAzimut;
                } else if (now_ms - stall_check_timer_az > STALL_TIME_MS) { // 3. ...han pasado 2 segundos.
                    // 4. Â¿Nos hemos movido menos del umbral?
                    if (fabs(currentAzimut - last_checked_az) < STALL_MOTION_THRESHOLD) {
                        // 5. Â¡ATASCO! Ordenamos mover y no hubo movimiento.
                        printf("Â¡Â¡Â¡ ATASCO DE AZIMUT DETECTADO !!!\n");
                        is_stalled_az = true;
                        g_az_step_delay_us = 0; // Â¡Parada de emergencia!
                    } else {
                        // 6. Nos estamos moviendo. Reiniciar el temporizador.
                        stall_check_timer_az = now_ms;
                        last_checked_az = currentAzimut;
                    }
                }
            } else {
                // 7. El PID ordenÃ³ detenerse. Resetear el temporizador.
                stall_check_timer_az = 0;
            }

            // --- Revisar Altitud --- (LÃ³gica idÃ©ntica)
            if (g_alt_step_delay_us > 0) {
                if (stall_check_timer_alt == 0) {
                    stall_check_timer_alt = now_ms;
                    last_checked_alt = filteredAltitud; // Usar valor filtrado
                } else if (now_ms - stall_check_timer_alt > STALL_TIME_MS) {
                    if (fabs(filteredAltitud - last_checked_alt) < STALL_MOTION_THRESHOLD) {
                        printf("Â¡Â¡Â¡ ATASCO DE ALTITUD DETECTADO !!!\n");
                        is_stalled_alt = true;
                        g_alt_step_delay_us = 0; // Â¡Parada de emergencia!
                    } else {
                        stall_check_timer_alt = now_ms;
                        last_checked_alt = filteredAltitud;
                    }
                }
            } else {
                stall_check_timer_alt = 0;
            }


            // -----------------------------------------------------
            // 5. ENVIAR DATOS A LA APP (BLE) - Â¡LÃ“GICA MODIFICADA!
            // -----------------------------------------------------
            if (now_ms - last_notify_ms >= 200) {

                if (is_stalled_az || is_stalled_alt) {
                    // A. Â¡Hay un atasco! Enviar el mensaje de error.
                    char *stall_msg = "STALL,STALL\n";
                    printf("ðŸ“¤ Enviando: %s", stall_msg);
                    att_server_notify(
                        ble_con_handle, 
                        ATT_CHARACTERISTIC_0000FFE2_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE, 
                        (uint8_t*)stall_msg, 
                        strlen(stall_msg)
                    );

                    // Resetear el estado de atasco (esperarÃ¡ a que el usuario arregle y reinicie el tracking)
                    is_stalled_az = false;
                    is_stalled_alt = false;

                } else {
                    // B. Todo normal. Enviar la posiciÃ³n.
                    ble_send_position(currentAzimut, filteredAltitud);
                }

                last_notify_ms = now_ms;
            }

        } else {
            // Fallo al leer sensores
            if (now_ms - last_debug_ms > 1000) { // Imprimir solo una vez por segundo
                printf("âŒ Fallo al leer MPU o MAG\n");
            }
        }

        // Imprimir estado general cada segundo
        if (now_ms - last_debug_ms >= 1000) {
            printf("\nâ”â”â” DEBUG ESTADO (loop=%lu) â”â”â”\n", loop_count);
            printf("   IMU: MPU=%s, MAG=%s\n", have_mpu ? "OK" : "FALLO", have_mag ? "OK" : "FALLO");
            printf("   BLE: Handle=0x%04X (%s), Notify=%s\n", 
                   ble_con_handle, 
                   ble_con_handle == HCI_CON_HANDLE_INVALID ? "INVALID" : "VALID",
                   notifications_enabled ? "ON" : "OFF");
            printf("   TARGET: Az=%.2f, Alt=%.2f\n", targetAzimut, targetAltitud);
            printf("   CURRENT: Az=%.2f, Alt=%.2f (Filtrado: %.2f)\n", currentAzimut, currentAltitud, filteredAltitud);
            printf("   STALL: Az=%s, Alt=%s\n", is_stalled_az ? "SI" : "NO", is_stalled_alt ? "SI" : "NO");
            printf("â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”â”\n\n");
            last_debug_ms = now_ms;
        }

        // Volver a programar este timer para la prÃ³xima iteraciÃ³n (10ms)
        btstack_run_loop_set_timer(ts, sample_period_ms);
        btstack_run_loop_add_timer(ts);
    }

#else
    static void periodic_timer_handler(btstack_timer_source_t *ts) {
    static uint32_t loop_count = 0;
    static uint64_t last_debug_ms = 0;
    static uint64_t last_notify_ms = 0;
    loop_count++;

    uint64_t now_ms = to_ms_since_boot(get_absolute_time());

    // --------------------------------
    // 1. PROCESAR DATOS ENTRANTES (UDP)
    // --------------------------------
    if (phone_imu_data.new_data) {
        // Normalizar ángulo recibido (0-360)
        float rawAz = phone_imu_data.azimut;
        while (rawAz < 0) rawAz += 360.0f;
        while (rawAz >= 360.0f) rawAz -= 360.0f;
        currentAzimut = rawAz;

        filteredAltitud = phone_imu_data.altitud; 
        
        phone_imu_data.new_data = false;
        last_udp_packet_ms = now_ms; // ¡Señal viva!
    }  

    // --------------------------------
    // 2. LÓGICA DE CONTROL Y SEGURIDAD
    // --------------------------------
    
    // A) Chequeo de Seguridad (Watchdog UDP)
    bool signal_lost = (now_ms - last_udp_packet_ms > 1000 && last_udp_packet_ms != 0);
    
    if (signal_lost) {
        // PARADA DE EMERGENCIA
        g_az_step_delay_us = 0;
        g_alt_step_delay_us = 0;
        
        if (last_udp_packet_ms != 0) { 
             printf("¡¡¡ WATCHDOG: Pérdida de señal UDP !!! MOTORES DETENIDOS.\n");
             last_udp_packet_ms = 0; // Reset para no spamear
        }
    }
    // B) Control Normal (Si sistema activo Y hay señal)
    else if (system_active && last_udp_packet_ms != 0) {

       
        
        // --- CALCULAR PID ---
        float outAz = ramp(&pidAz, targetAzimut, currentAzimut);
        float outAlt = pid_compute(&pidAlt, targetAltitud, filteredAltitud, DEADBANDALT);
         printf("outAz %f.2, outalt:%f.2 \n", outAz, outAlt);
        // --- MOTOR AZIMUT ---
        if (is_stalled_az) {
            g_az_step_delay_us = 0;
        } else {
            // AQUI USAMOS EL SIGNO QUE DEVOLVIÓ LA FUNCIÓN
            gpio_put(DIR_AZ_PIN, (outAz < 0)); 
        
            if (fabs(outAz) < 1.0f) { // Si la velocidad es casi 0
                g_az_step_delay_us = 0; 
            } else {
                // Convertir Velocidad a Delay (Inversamente proporcional)
                // Mayor outAz = Menor Delay = Motor más rápido
                uint32_t delay = (uint32_t)(VELOCITY_CONSTANT / fabs(outAz));

                // Limitar los delays físicos del motor
                if (delay > MAX_DELAY_US) delay = MAX_DELAY_US; 
                if (delay < MIN_STEP_DELAY_US) delay = MIN_STEP_DELAY_US; 

                g_az_step_delay_us = delay;
            }
        }

       

        
        if (is_stalled_alt) {
            g_alt_step_delay_us = 0;
        } else {
            // AQUI USAMOS EL SIGNO QUE DEVOLVIÓ LA FUNCIÓN
            gpio_put(DIR_ALT_PIN, (outAlt < 0)); // Invertido también por si acaso
        
            if (fabs(outAlt) < 1.0f) { // Si la velocidad es casi 0
                 g_alt_step_delay_us = 0;
            } else {
                uint32_t delay = (uint32_t)(VELOCITY_CONSTANT / fabs(outAlt));
                if (delay > MAX_DELAY_US) delay = MAX_DELAY_US;
                if (delay < MIN_STEP_DELAY_US) delay = MIN_STEP_DELAY_US;
                g_alt_step_delay_us = delay;
            }
        }

        // --- DETECCIÓN DE ATASCO (STALL) ---
        // (Azimut)
        if (g_az_step_delay_us > 0) {
            if (stall_check_timer_az == 0) {
                stall_check_timer_az = now_ms;
                last_checked_az = currentAzimut;
            } else if (now_ms - stall_check_timer_az > STALL_TIME_MS) {
                float mov = currentAzimut - last_checked_az;
                // Manejar el salto de 360 a 0
                while (mov > 180) mov -= 360;
                while (mov < -180) mov += 360;
                
                if (fabs(mov) < STALL_MOTION_THRESHOLD) {
                    printf("¡¡¡ ATASCO AZIMUT !!!\n");
                    is_stalled_az = true;
                    g_az_step_delay_us = 0;
                } else {
                    stall_check_timer_az = now_ms; // Reset timer
                    last_checked_az = currentAzimut;
                }
            }
        } else {
            stall_check_timer_az = 0;
            is_stalled_az = false; // Auto-reset si el PID para
        }

        // (Altitud) - Lógica simplificada
        if (g_alt_step_delay_us > 0) {
            if (stall_check_timer_alt == 0) {
                stall_check_timer_alt = now_ms;
                last_checked_alt = filteredAltitud;
            } else if (now_ms - stall_check_timer_alt > STALL_TIME_MS) {
                if (fabs(filteredAltitud - last_checked_alt) < STALL_MOTION_THRESHOLD) {
                    printf("¡¡¡ ATASCO ALTITUD !!!\n");
                    is_stalled_alt = true;
                    g_alt_step_delay_us = 0;
                } else {
                    stall_check_timer_alt = now_ms;
                    last_checked_alt = filteredAltitud;
                }
            }
        } else {
            stall_check_timer_alt = 0;
            is_stalled_alt = false;
        }

    } else {
        // C) Sistema INACTIVO o sin datos iniciales -> Todo quieto
        g_az_step_delay_us = 0;
        g_alt_step_delay_us = 0;
    }

    // --------------------------------
    // 3. REPORTE Y COMUNICACIÓN
    // --------------------------------
    if (now_ms - last_notify_ms >= 200) {
        if (is_stalled_az || is_stalled_alt) {
            char *stall_msg = "STALL,STALL\n";
            att_server_notify(ble_con_handle, ATT_CHARACTERISTIC_0000FFE2_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE, (uint8_t*)stall_msg, strlen(stall_msg));
        } else {
            ble_send_position(currentAzimut, filteredAltitud);
        }
        last_notify_ms = now_ms;
    }

    if (now_ms - last_debug_ms >= 1000) {
        printf("\n--- ESTADO (loop=%lu) ---\n", loop_count);
        printf("   IMU UDP: %s\n", (last_udp_packet_ms > 0) ? "OK" : "ESPERANDO");
        printf("   TARGET:  %.2f, %.2f\n", targetAzimut, targetAltitud);
        printf("   CURRENT: %.2f, %.2f\n", currentAzimut, filteredAltitud);
        printf("-------------------------\n");
        last_debug_ms = now_ms;
    }

    static uint64_t last_print_ms = 0;
if (now_ms - last_print_ms >= 100) {
    // Formato CSV: tiempo,target,current,error,P,I,D,output,delay
    printf("PID_AZ,%llu,%.2f,%.2f,%.2f,%.2f,%.2f,%lu\n",
           now_ms,
           targetAzimut,
           currentAzimut,
           targetAzimut - currentAzimut,  // error
           pidAz.Kp * (targetAzimut - currentAzimut),  // P
           pidAz.Ki * pidAz.integral,  // I
           -pidAz.Kd * pidAz.filtered_derivative,  // D
           (unsigned long)g_az_step_delay_us
    );
    
    last_print_ms = now_ms;
}

    btstack_run_loop_set_timer(ts, sample_period_ms);
    btstack_run_loop_add_timer(ts);
}
#endif

 

bool stepper_pulse_generator(struct repeating_timer *t) {
    static uint64_t last_az_pulse_time = 0;
    static uint64_t last_alt_pulse_time = 0;
    uint64_t now = time_us_64();

    // --- Motor Azimut ---
    if (g_az_step_delay_us > 0) { // Â¿Debe moverse?
        if (now - last_az_pulse_time > g_az_step_delay_us) {
            gpio_put(STEP_AZ_PIN, 1);
            sleep_us(2); // Pulso corto (2Âµs es suficiente para la mayorÃ­a de drivers)
            gpio_put(STEP_AZ_PIN, 0);
            last_az_pulse_time = now; // Registrar la hora de este pulso
        }
    }

    // --- Motor Altitud ---
    if (g_alt_step_delay_us > 0) { // Â¿Debe moverse?
        if (now - last_alt_pulse_time > g_alt_step_delay_us) {
            gpio_put(STEP_ALT_PIN, 1);
            sleep_us(2); // Pulso corto
            gpio_put(STEP_ALT_PIN, 0);
            last_alt_pulse_time = now; // Registrar la hora de este pulso
        }
    }
    
    return true; // Mantener el temporizador en marcha
}

// -----------------------------------------------------------------------------
// SECCIÃ“N 7: FUNCIÃ“N PRINCIPAL (main)
// -----------------------------------------------------------------------------
int main() {
    stdio_init_all();
    
    printf("=================================\n");
    printf("Horizonte Hardware\n");
    printf("=================================\n");
    
    // Temporizador de inicio
    for (int i = 5; i > 0; i--) {
        printf("Iniciando en %d...\n", i);
        sleep_ms(1000);
    }
    printf("\n");

    // PASO 1: Inicializar WiFi/BLE (requerido para BTstack)
    printf("ðŸ”§ Paso 1: Inicializando WiFi/BLE...\n");
    if (cyw43_arch_init()) {
        printf("âŒ ERROR: cyw43_arch_init fallÃ³\n");
        return -1;
    }
    printf("âœ… WiFi/BLE OK\n\n");
    sleep_ms(100);

    
    #if IMU
        // PASO 2: Inicializar I2C
        printf("ðŸ”§ Paso 2: Inicializando I2C...\n");
        i2c_init(I2C_PORT, 400000); // 400 kHz fast mode
        gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
        gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
        //Nota: Las resistencias pull-up externas en la placa del MPU son preferibles
        //a las internas del Pico.
        gpio_pull_up(I2C_SDA_PIN);
        gpio_pull_up(I2C_SCL_PIN);
        sleep_ms(200);
        printf("âœ… I2C inicializado\n");

        // PASO 3: Inicializar Sensores (MPU + Mag) y Calibrar
        printf("\nðŸ”§ Paso 3: Inicializando MPU9250...\n");
        if (!mpu_init()) {
            printf("âŒ ERROR: No se pudo iniciar MPU9250\n");
            // Ejecutar diagnÃ³stico si falla
            diagnostico_i2c();
        } else {
            printf("âœ… MPU9250 inicializado\n");

            // (Lectura de prueba inicial)
            float ax, ay, az, gx, gy, gz;
            if (mpu_read_accel_gyro(&ax, &ay, &az, &gx, &gy, &gz)) {
                printf("   âœ… Lectura de prueba OK (Accel Z: %.2f m/sÂ²)\n", az);
            } else {
                printf("   âŒ Lectura de prueba FALLÃ“\n");
            }

            // Ejecutar diagnÃ³stico para confirmar
            diagnostico_i2c();

            // Inicializar MagnetÃ³metro
            ak8963_init();

            // Calibrar Giroscopio (Â¡MUY IMPORTANTE!)
            calibrar_giroscopio(200);
        }

        // PASO 4: Inicializar Filtro
        printf("ðŸ”§ Paso 4: Inicializando Madgwick...\n");
        madgwick_init(&mad, BETA);
        printf("âœ… Madgwick OK\n");
        

    #else
        cyw43_arch_enable_sta_mode();
        printf("   Conectando a Wi-Fi...\n");
        while (true) {
            int ret = cyw43_arch_wifi_connect_timeout_ms("IMU", "Horizonte", CYW43_AUTH_WPA2_AES_PSK, 30000);

            // 1. Comprobar si tuvo Ã‰XITO
            if (ret == 0) {
                // Â¡Ã‰xito! Salimos del bucle.
                break; 
            }

            // 2. Si llegamos aquÃ­, es porque fallÃ³.
            //    Ahora SÃ imprimimos el error.
            printf(" ERROR: Falla al conectar (CÃ³digo: %d)\n", ret);
            printf("Reintentando en 5 segundos...\n");

            // 3. (Importante) Esperar antes de reintentar
            sleep_ms(5000); 
        }

        printf("âœ… Wi-Fi Conectado!\n");
        // Imprimir la IP AHORA que sÃ­ la tenemos
        printf("   IP: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    #endif

    printf("ðŸ”§ Paso 5: Inicializando PIDs...\n");
    // Â¡Â¡Â¡ ATENCIÃ“N !!! Tus valores de Kp, Ki, Kd ANTERIORES no servirÃ¡n.
    // Estaban ajustados para "Ã¡ngulo". Ahora controlan "velocidad".
    // Empieza con valores MUCHO MÃS BAJOS y ajÃºstalos.
    pid_init(&pidAz, 50.0f, 0.1f, 20.0f);
    pid_init(&pidAlt, 3.0f, 0.1f, 0.4f);
    printf("âœ… PIDs OK\n");

    // PASO 6: Inicializar Control de Motores (Steppers)
    printf("ðŸ”§ Paso 6: Inicializando Drivers de Stepper...\n");
    
    // Inicializar los 4 pines de control como SALIDA
    gpio_init(STEP_AZ_PIN);
    gpio_set_dir(STEP_AZ_PIN, GPIO_OUT);
    gpio_init(DIR_AZ_PIN);
    gpio_set_dir(DIR_AZ_PIN, GPIO_OUT);
    
    gpio_init(STEP_ALT_PIN);
    gpio_set_dir(STEP_ALT_PIN, GPIO_OUT);
    gpio_init(DIR_ALT_PIN);
    gpio_set_dir(DIR_ALT_PIN, GPIO_OUT);

    // Inicializar y activar el pin ENABLE
    gpio_init(DRIVER_ENABLE_PIN);
    gpio_set_dir(DRIVER_ENABLE_PIN, GPIO_OUT);
    set_drivers_enable(false); // Activar los drivers usando la funciÃ³n dedicada
    
    // Verificar que el pin realmente cambiÃ³
    printf("   ðŸ” VerificaciÃ³n: PIN %d = %d (deberÃ­a ser 0)\n", DRIVER_ENABLE_PIN, gpio_get(DRIVER_ENABLE_PIN));
    if (gpio_get(DRIVER_ENABLE_PIN) != 0) {
        printf("   âš ï¸  ADVERTENCIA: El pin ENABLE no estÃ¡ en LOW\n");
    }
 

    printf("âœ… Drivers OK\n\n");



    // PASO 7: Configurar BTstack (BLE)
    printf("ðŸ”§ Paso 7: Configurando BTstack...\n");
    l2cap_init();
    sm_init(); // Security Manager
    sm_set_authentication_requirements(0); // Sin autenticaciÃ³n
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);

    // Cargar el perfil GATT (servicios/caracterÃ­sticas)
    extern const uint8_t profile_data[];
    att_server_init(profile_data, NULL, att_write_callback);

    // Configurar el anuncio (Advertising)
    const char * device_name = "BT04-A";
    uint8_t adv_data[31];
    uint8_t adv_data_len = 0;
    adv_data[adv_data_len++] = 2;
    adv_data[adv_data_len++] = BLUETOOTH_DATA_TYPE_FLAGS;
    adv_data[adv_data_len++] = 0x06; // LE General Discoverable, BR/EDR not supported
    uint8_t name_len = strlen(device_name);
    adv_data[adv_data_len++] = name_len + 1;
    adv_data[adv_data_len++] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(&adv_data[adv_data_len], device_name, name_len);
    adv_data_len += name_len;
    
    // (Opcional: aÃ±adir el Service UUID al anuncio)
    // adv_data[adv_data_len++] = 3;
    // adv_data[adv_data_len++] = BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_16_BIT_SERVICE_UUIDS;
    // adv_data[adv_data_len++] = 0xE0; // FFE0 (little-endian)
    // adv_data[adv_data_len++] = 0xFF;
    
    gap_advertisements_set_data(adv_data_len, adv_data);
    gap_scan_response_set_data(0, NULL);
    
    // Registrar el manejador de eventos principal
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    
    // servidor UDO
    hci_power_control(HCI_POWER_ON);
    printf("âœ… BTstack configurado\n\n");

    #if !IMU 
        // Â¡NUEVO! Configurar el servidor UDP
        printf("ðŸ”§ Iniciando servidor UDP en puerto %d...\n", UDP_PORT);
        struct udp_pcb *pcb = udp_new();
        if (pcb == NULL) {
            printf("âŒ ERROR: No se pudo crear el PCB de UDP\n");
        } else {
            // Vincular al puerto y a CUALQUIER direcciÃ³n IP
            if (udp_bind(pcb, IP_ADDR_ANY, UDP_PORT) != ERR_OK) {
                 printf("âŒ ERROR: Falla al hacer bind en UDP\n");
            } else {
                // Registrar nuestra funciÃ³n de callback
                udp_recv(pcb, udp_recv_callback, NULL);
                printf("âœ… Servidor UDP escuchando\n");
            }
        }
    #endif
    // PASO 8: Iniciar el bucle principal
    printf("ðŸ”§ Paso 8: Iniciando timer principal (a %d Hz)...\n", (int)SAMPLE_FREQ_HZ);
    main_loop_timer.process = &periodic_timer_handler;
    btstack_run_loop_set_timer(&main_loop_timer, sample_period_ms);
    btstack_run_loop_add_timer(&main_loop_timer);

    printf("ðŸ”§ Paso 9: Iniciando generador de pulsos (MÃºsculo)...\n");
    static struct repeating_timer stepper_timer;
    add_repeating_timer_us(50, stepper_pulse_generator, NULL, &stepper_timer);
    printf("âœ… Generador de pulsos activo\n");

    printf("=================================\n");
    printf("âœ… SISTEMA INICIADO\n");
    printf("=================================\n\n");

    // Entregar el control al bucle de BTstack
    btstack_run_loop_execute();

    return 0;
}