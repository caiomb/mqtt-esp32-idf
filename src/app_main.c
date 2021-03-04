// Bibliotecas C
#include <stdio.h>
#include <stddef.h>
#include <string.h>

// Bibliotecas do ESP32
#include <esp_wifi.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <mqtt_client.h>
#include <driver/gpio.h>

// Bibliotecas do FreeRTOS
#include <freertos\FreeRTOS.h>
#include <freertos\task.h>
#include <freertos\semphr.h>
#include <freertos\queue.h>
#include <freertos\event_groups.h>

// Bibliotecas de rede
#include <lwip\sockets.h>
#include <lwip\dns.h>
#include <lwip\netdb.h>

// Include da lib do sensor dht
#include "DHT.h"

// Defines para referencias de elementos

// Inicialização de elementos
static const char *TAG = "MQTT_IOT";
static const char *topic_mqtt_cmd = "set/apartamento/escritorio/luzes";
static const char *topic_mqtt_temp_data = "get/apartamento/escritorio/temperatura";
static const char *topic_mqtt_umid_data = "get/apartamento/escritorio/umidade";
static const char *topic_mqtt_luzes_data = "get/apartamento/escritorio/luzes";
char mqtt_buffer_temp[128];
char mqtt_buffer_umid[128];
char mqtt_buffer_light[128];

// Referencia para saber status de conexão
static EventGroupHandle_t wifi_event_group;
const static int CONNECTION_STATUS = BIT0;

int light_status = 0;

// Cliente MQTT
esp_mqtt_client_handle_t mqtt_client;

// Espaço para criação de tasks
void DHT_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Iniciando task DHT22...");
    setDHTgpio(GPIO_NUM_4);

    while (1)
    {
        ESP_LOGI(TAG, "Lendo dados de temperatura e umidade...");
        int ret = readDHT();
        errorHandler(ret);

        sprintf(mqtt_buffer_temp, "%.2f", getTemperature());
        sprintf(mqtt_buffer_umid, "%.2f", getHumidity());
        sprintf(mqtt_buffer_light, "%d", light_status);

        // Sanity check do mqtt_client antes de publicar
        esp_mqtt_client_publish(mqtt_client, topic_mqtt_temp_data, mqtt_buffer_temp, 0, 0, 0);
        esp_mqtt_client_publish(mqtt_client, topic_mqtt_umid_data, mqtt_buffer_umid, 0, 0, 0);
        esp_mqtt_client_publish(mqtt_client, topic_mqtt_luzes_data, mqtt_buffer_light, 0, 0, 0);

        ESP_LOGI(TAG, "Hum: %.1f Tmp: %.1f\n", getHumidity(), getTemperature());
        vTaskDelay(5000 / portTICK_RATE_MS);
    }
}

// callback para tratar eventos MQTT
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event){
    esp_mqtt_client_handle_t client = event->client;

    int msg_id;
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Conexao Realizada com Broker MQTT");
        msg_id = esp_mqtt_client_subscribe(client, topic_mqtt_cmd, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Desconexão Realizada com Broker MQTT");
        break;
    
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "Subscribe Realizado com Broker MQTT");
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "Publishe Realizado com Broker MQTT - msg_id");
        break;

    // Evento de chegada de mensagens
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Dados recebidos via MQTT");
        printf("TOPIC = %.*s\r\n", event->topic_len, event->topic);
        printf("DATA = %.*s\r\n", event->data_len, event->data);
        gpio_set_level(GPIO_NUM_2, atoi(event->data));
        light_status = atoi(event->data);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT EVENT ERROR");
        break;
    
    default:
        break;
    }
    return ESP_OK;
}

static esp_err_t wifi_event_handler (void *ctx, system_event_t *event)
{
    switch (event->event_id)
    {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;

    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTION_STATUS);
        break;

    case SYSTEM_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();  // Tenta conectar de novo
        xEventGroupClearBits(wifi_event_group, CONNECTION_STATUS); // Limpa status de conexao
        break;
    
    default:
        break;
    }
    return ESP_OK;
}

// Inicializacao da conexao com a rede WiFi
void wifi_init(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(wifi_event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "CLARO_2G681EBC",
            .password = "7D681EBC",
        },
    };

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_LOGI(TAG, "Iniciando Conexao com Rede WiFi...");
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_LOGI(TAG, "Conectando...");

    // Aguarda até que a conexão seja estabelecida com sucesso 
    xEventGroupWaitBits(wifi_event_group, CONNECTION_STATUS, false, true, portMAX_DELAY);
}

static void mqtt_init(void)
{
    const esp_mqtt_client_config_t mqtt_cfg= {
        .uri = "mqtt://test.mosquitto.org:1883",
        .event_handle = mqtt_event_handler,
        .client_id = "ESP32_IOT_CBRAGA",
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(mqtt_client);
}

static void hw_init(void)
{
    gpio_pad_select_gpio(GPIO_NUM_2);
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 0);
}

void app_main()
{
    ESP_LOGI(TAG, "Iniciando ESP32 IoT App...");
    
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);

    esp_err_t ret = nvs_flash_init();
    if(ret == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Iniciando wifi...");
    wifi_init();

    ESP_LOGI(TAG, "Iniciando MQTT...");
    mqtt_init();

    ESP_LOGI(TAG, "Configurando HW...");
    hw_init();

    ESP_LOGI(TAG, "Criando DHT Task...");
    xTaskCreate(&DHT_task, "DHT_task", 2048, NULL, 5, NULL);

    while(1)
    {
        vTaskDelay(5000/portTICK_RATE_MS);
    }
}