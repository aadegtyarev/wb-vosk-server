#include <HTTPClient.h>
#include <WiFi.h>
#include <driver/i2s.h>
#include <WebSocketsClient.h>

#include "M5Atom.h"

// Настройки Wi-Fi
const char *WifiSSID = "WB-LAB";  // Имя сети Wi-Fi
const char *WifiPWD  = "08877922";  // Пароль от сети Wi-Fi
const char *ClientId =  "M5 Atom Echo 1" ;
const char *ServerIP = "192.168.2.117";   // IP-адрес сервера
const int ServerPort = 8765;    // Порт для подключения к серверу

// Настройки пинов для I2S
#define CONFIG_I2S_BCK_PIN     19  // Пин для тактового сигнала (BCK)
#define CONFIG_I2S_LRCK_PIN    33  // Пин для сигнала выбора канала (LRCK)
#define CONFIG_I2S_DATA_PIN    22  // Пин для передачи данных (DATA)
#define CONFIG_I2S_DATA_IN_PIN 23  // Пин для приема данных (DATA IN)

#define SPEAK_I2S_NUMBER I2S_NUM_0  // Используемый I2S-порт

// Режимы работы: микрофон или динамик
#define MODE_MIC 0
#define MODE_SPK 1

#define DATA_SIZE 1024  // Размер данных для чтения

WebSocketsClient webSocket;  // Объект для работы с WebSocket

// Функция инициализации I2S для работы в режиме микрофона или динамика
bool InitI2SSpeakOrMic(int mode) {
    esp_err_t err = ESP_OK;

    // Удаляем драйвер I2S, если он был установлен ранее
    i2s_driver_uninstall(SPEAK_I2S_NUMBER);

    // Настройка конфигурации I2S
    i2s_config_t i2s_config = {
        .mode        = (i2s_mode_t)(I2S_MODE_MASTER),  // Режим мастера
        .sample_rate = 16000,  // Частота дискретизации
        .bits_per_sample =
            I2S_BITS_PER_SAMPLE_16BIT,  // Разрядность выборки (16 бит)
        .channel_format = I2S_CHANNEL_FMT_ALL_RIGHT,  // Формат канала
#if ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(4, 1, 0)
        .communication_format =
            I2S_COMM_FORMAT_STAND_I2S,  // Формат коммуникации (стандартный I2S)
#else                                   
        .communication_format = I2S_COMM_FORMAT_I2S,
#endif
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,  // Флаги прерываний
        .dma_buf_count    = 6,  // Количество буферов DMA
        .dma_buf_len      = 60,  // Длина буфера DMA
    };

    // Настройка режима работы (микрофон или динамик)
    if (mode == MODE_MIC) {
        i2s_config.mode =
            (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);  // Режим микрофона
    } else {
        i2s_config.mode     = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);  // Режим динамика
        i2s_config.use_apll = false;  // Отключение APLL
        i2s_config.tx_desc_auto_clear = true;  // Автоматическая очистка дескрипторов передачи
    }

    Serial.println("[I2S] Initializing I2S driver...");

    // Установка драйвера I2S
    err += i2s_driver_install(SPEAK_I2S_NUMBER, &i2s_config, 0, NULL);

    // Настройка пинов I2S
    i2s_pin_config_t tx_pin_config;

#if (ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(4, 3, 0))
    tx_pin_config.mck_io_num = I2S_PIN_NO_CHANGE;  // Пин MCK (не используется)
#endif
    tx_pin_config.bck_io_num   = CONFIG_I2S_BCK_PIN;  // Пин BCK
    tx_pin_config.ws_io_num    = CONFIG_I2S_LRCK_PIN;  // Пин LRCK
    tx_pin_config.data_out_num = CONFIG_I2S_DATA_PIN;  // Пин DATA OUT
    tx_pin_config.data_in_num  = CONFIG_I2S_DATA_IN_PIN;  // Пин DATA IN

    Serial.println("[I2S] Setting I2S pins...");
    err += i2s_set_pin(SPEAK_I2S_NUMBER, &tx_pin_config);  // Установка пинов

    Serial.println("[I2S] Setting I2S clock...");
    err += i2s_set_clk(SPEAK_I2S_NUMBER, 16000, I2S_BITS_PER_SAMPLE_16BIT,
                       I2S_CHANNEL_MONO);  // Установка частоты и формата данных

    return true;
}

// Буфер для хранения данных с микрофона
static uint8_t microphonedata0[1024 * 70];
size_t byte_read = 0;  // Количество прочитанных байт
int16_t *buffptr;  // Указатель на буфер
uint32_t data_offset = 0;  // Смещение в буфере

// Обработчик событий WebSocket
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println(String("[WS] Disconnected from server ")+String(ServerIP)+String(":"+ String(ServerPort)));
            break;
        case WStype_CONNECTED:
            Serial.println("[WS] Connected to server ")+String(ServerIP)+String(":"+ String(ServerPort));
            webSocket.sendTXT(String("{\"client_id\": \"") + String(ClientId) + String("\"}"));
            break;
        case WStype_TEXT:
            Serial.print("[WS] Server message: ");
            Serial.println((char*)payload);  
            break;
        case WStype_BIN:
            Serial.println("[WS] Received binary data");  
            break;
    }
}

void setup() {
    M5.begin(true, false, true);  // Инициализация M5Atom
    M5.dis.clear();  // Очистка дисплея

    Serial.println("[I2S] Initializing speaker...");
    InitI2SSpeakOrMic(MODE_SPK);  // Инициализация I2S в режиме динамика
    delay(100);

    Serial.println("[WiFi] Connecting to WiFi...");
    WiFi.mode(WIFI_STA);  // Установка режима Wi-Fi (станция)
    WiFi.setSleep(false);  // Отключение режима сна Wi-Fi
    WiFi.begin(WifiSSID, WifiPWD);  // Подключение к Wi-Fi

    M5.dis.drawpix(0, CRGB(0, 128, 0));  // Индикация подключения к Wi-Fi (зеленый)

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }

    M5.dis.drawpix(0, CRGB(128, 0, 0));  // Индикация успешного подключения (красный)

    Serial.println("\n[WiFi] Connected.");

    // Инициализация WebSocket
    webSocket.begin(ServerIP, ServerPort, "/");
    webSocket.onEvent(webSocketEvent);  // Установка обработчика событий
    webSocket.setReconnectInterval(5000);  // Интервал переподключения
}

String SpakeStr;  // Строка для хранения данных
bool Spakeflag = false;  // Флаг для управления записью

void loop() {
    webSocket.loop();  // Обработка событий WebSocket

    // Если кнопка нажата, начинаем запись с микрофона

    if (M5.Btn.isPressed()) {
        data_offset = 0;  // Сброс смещения в буфере
        Spakeflag   = false;  // Сброс флага записи
        InitI2SSpeakOrMic(MODE_MIC);  // Инициализация I2S в режиме микрофона
        M5.dis.drawpix(0, CRGB(128, 128, 0));  // Индикация записи (желтый)

        Serial.println("[AUDIO] Starting audio transmission to WebSocket...");

        // Цикл записи данных с микрофона и их отправки
        while (1) {
            // Чтение данных с микрофона
            i2s_read(SPEAK_I2S_NUMBER, (char *)(microphonedata0 + data_offset),
                     DATA_SIZE, &byte_read, (100 / portTICK_RATE_MS));

            // Если данные успешно прочитаны
            if (byte_read > 0) {
                // Отправка данных через WebSocket
                if (!webSocket.sendBIN(microphonedata0 + data_offset, byte_read)) {
                    Serial.println("[AUDIO] Error sending audio data");  // Ошибка отправки
                }

                // Освобождение буфера
                data_offset = 0;
            }

            M5.update();  // Обновление состояния кнопки
            if (M5.Btn.isReleased()) break;  // Выход из цикла, если кнопка отпущена
        }

        Serial.println("[AUDIO] Audio transmission completed.");

        // Индикация завершения записи
        M5.dis.drawpix(0, CRGB(128, 0, 128));  // Индикация успешной отправки (фиолетовый)
    }

    // Проверка подключения к Wi-Fi
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Reconnecting...");
        WiFi.reconnect();  // Переподключение к Wi-Fi
        while (WiFi.status() != WL_CONNECTED) {
            delay(100);
        }
    }
    M5.update();  // Обновление состояния кнопки
    delay(100);
}
