#include <stdio.h>
#include "pico/stdlib.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "pico/bootrom.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "MultiTaferas2.pio.h"
#include "math.h"
#include "queue.h"

//---------------------------------------------------
// DEFINES
//---------------------------------------------------
#define I2C_PORT        i2c1
#define I2C_SDA         14
#define I2C_SCL         15
#define ENDERECO        0x3C
#define LED_RED         13
#define LED_BLUE         12
#define LED_MATRIX      7
#define BUZZER          21
#define BUTTON_B         6
#define JOYSTICK_X      26
#define JOYSTICK_Y      27
#define TOTAL_LEDS      25
#define MAX_LED_VALUE   30

#define MAX_FUNC(a, b)  (((a) >= (b)) ? (a) : (b))
#define MIN_FUNC(a, b)  (((a) <= (b)) ? (a) : (b))


typedef struct
{
    int16_t nivel_rio;
    int16_t volume_chuva;
    bool alerta;
    bool origem;
} dados_t;

QueueHandle_t xQueueDados;

void vJoystickTask(void *params);
void vDisplayTask(void *params);
void vLedTask(void *params);
void vBuzzerTask(void *params);
void vMatrizTask(void *params);
void gpio_irq_handler(uint gpio, uint32_t events);

int main()
{
    // Para ser utilizado o modo BOOTSEL com botão B
    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_B);
    gpio_set_irq_enabled_with_callback(BUTTON_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    stdio_init_all();

    xQueueDados = xQueueCreate(20, sizeof(dados_t));

    xTaskCreate(vJoystickTask, "JoystikTask", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY+1, NULL);
    xTaskCreate(vDisplayTask, "DisplayTask", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vLedTask, "LedTask", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vBuzzerTask, "BuzzerTask", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vMatrizTask, "vMatrizTask", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
    vTaskStartScheduler();
    panic_unsupported();
}

void vJoystickTask(void *params)
{
    adc_gpio_init(JOYSTICK_Y);
    adc_gpio_init(JOYSTICK_X);
    adc_init();
    uint16_t adc_x;
    uint16_t adc_y;
    uint16_t tolerancia = 250; //Tolerancia para filtra pequenos desvios
    uint16_t center_value_y = 2000;  // Valor medido experimentalmente
    uint16_t center_value_x = 1960;  // Valor medido experimentalmente
    uint8_t passo = 1; //Passo de incremetação dos variáveis medidas

    dados_t dados;
    dados.nivel_rio = 50;
    dados.volume_chuva = 30;

    while (true)
    {
        adc_select_input(0); // GPIO 26 = ADC0
        adc_y = adc_read();
        adc_select_input(1); // GPIO 27 = ADC1
        adc_x = adc_read();
        //Filtro de ruídos
        int16_t leitura_x = (abs(adc_x - center_value_x) > tolerancia) ? adc_x - center_value_x :  0;
        int16_t leitura_y = (abs(adc_y - center_value_y) > tolerancia) ? adc_y - center_value_y : 0;
        if (leitura_y != 0){
        dados.nivel_rio = (int) ((leitura_y > 0)? dados.nivel_rio + passo : dados.nivel_rio - passo );
        dados.nivel_rio = MAX_FUNC(MIN_FUNC(dados.nivel_rio, 100), 0); //Normalização
        }
        if (leitura_x != 0){
        dados.volume_chuva = (int) ((leitura_x > 0)? dados.volume_chuva + passo : dados.volume_chuva - passo);
        dados.volume_chuva = MAX_FUNC(MIN_FUNC(dados.volume_chuva, 100), 0); //Normalização
        }
        if (dados.nivel_rio > 70 || dados.volume_chuva > 80){
            dados.alerta = true;
            dados.origem = ((dados.nivel_rio > 70)? true: false);
        }else{
            dados.alerta = false;
        }
        xQueueSend(xQueueDados,&dados,0);
        printf("VALOR X: %d ! \n", leitura_x );
        printf("VALOR Y: %d ! \n", leitura_y );
        vTaskDelay(pdMS_TO_TICKS(100));              // 10 Hz de leitura
    }
}
void vDisplayTask(void *params){

    dados_t dados;
    ssd1306_t ssd;
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    i2c_init(I2C_PORT, 400000); // 0.4 MHz
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, ENDERECO, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    char string_volume[4];
    char string_nivel[4];
    char string_alerta[10];


    while (true)
    {   
        if (xQueueReceive(xQueueDados, &dados, portMAX_DELAY) == pdTRUE){

            sprintf(string_volume,"%d", dados.volume_chuva);
            sprintf(string_nivel,"%d", dados.nivel_rio);

            ssd1306_fill(&ssd, false);
            ssd1306_rect(&ssd, 1, 1, 126, 62, 1, 1); // Borda grossa
            ssd1306_rect(&ssd, 4, 4, 120, 56, 0, 1); // Borda grossa
            ssd1306_draw_string(&ssd, "NIVEL: ", 5, 7);
            ssd1306_draw_string(&ssd, string_nivel, 70, 7);
            ssd1306_draw_string(&ssd, "%", 100, 7);
            ssd1306_draw_string(&ssd, "VOLUME:", 5, 17);
            ssd1306_draw_string(&ssd, string_volume, 70, 17);
            ssd1306_draw_string(&ssd, "%", 100, 17);
            if(dados.alerta){
                ssd1306_draw_string(&ssd, "ALERTA!", 5, 27);
                if(dados.origem){
                    ssd1306_draw_string(&ssd, "RIO CHEIO!", 5, 37);
                }else{
                    ssd1306_draw_string(&ssd, "CHUVA FORTE!", 5, 37);
                }
            }
            ssd1306_send_data(&ssd);     
    }else{
        printf("DEU RUIM!");
    }
    vTaskDelay(pdMS_TO_TICKS(200)); 

}}
void vLedTask(void *params){

    dados_t dados;
    gpio_init(LED_RED);
    gpio_set_dir(LED_RED, GPIO_OUT);
    gpio_init(LED_BLUE);
    gpio_set_dir(LED_BLUE, GPIO_OUT);

    while(true){
        if (xQueueReceive(xQueueDados, &dados, portMAX_DELAY) == pdTRUE){
            if (dados.alerta){
                if(dados.origem){
                    gpio_put(LED_BLUE,0);
                    gpio_put(LED_RED,1);
                    vTaskDelay(pdMS_TO_TICKS(300));
                }else{
                    gpio_put(LED_BLUE,0);
                    gpio_put(LED_RED,1);
                    vTaskDelay(pdMS_TO_TICKS(300));
                    gpio_put(LED_RED,0);
                    vTaskDelay(pdMS_TO_TICKS(300));
                }
            }else{
                gpio_put(LED_BLUE,1);
                gpio_put(LED_RED,0);
                vTaskDelay(pdMS_TO_TICKS(300));
        }

        }else{
            printf("Fila Vazia!ERRO!\n");

    }}
}
void vBuzzerTask(void *params){
    dados_t dados;
// PWM BUZZER
    gpio_set_function(BUZZER, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(BUZZER); 
    const uint16_t wrap = 1000;   // Valor de wrap do PWM
    pwm_set_enabled(slice, true);
    pwm_set_wrap(slice, wrap);
    pwm_set_clkdiv(slice, 128); //1Khz 

    while(true){
        if (xQueueReceive(xQueueDados, &dados, portMAX_DELAY) == pdTRUE){
        if(dados.alerta){
            pwm_set_gpio_level(BUZZER, 500);
            vTaskDelay(pdMS_TO_TICKS(500));
            pwm_set_gpio_level(BUZZER,0);
            vTaskDelay(pdMS_TO_TICKS(500));
        }else{
            pwm_set_gpio_level(BUZZER, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
    }}}}
    void vMatrizTask(void *params){
    dados_t dados;
    PIO pio = pio0;
    bool clk = set_sys_clock_khz(128000, false);
    uint offset = pio_add_program(pio, &MultiTaferas2_program);
    uint sm_pio = pio_claim_unused_sm(pio, true);
    MultiTaferas2_program_init(pio, sm_pio, offset, LED_MATRIX);

    while(true){
    if (xQueueReceive(xQueueDados, &dados, portMAX_DELAY) == pdTRUE){
        int8_t cor_steps = (dados.alerta) ? 16 : 8;
        // Mapeia volume [0, volume_max] para [0, TOTAL_LEDS] (valor fracionário)
        double leds_ativos = (TOTAL_LEDS * dados.nivel_rio) / 100;
        int leds_full = (int)leds_ativos;
        double led_parcial = fmod(leds_ativos, 1.0);
        int brilho_parcial = (int)(led_parcial * MAX_LED_VALUE);
    
        uint32_t valor_full = ((uint32_t)MAX_LED_VALUE) << cor_steps;
        uint32_t valor_parcial = ((uint32_t)brilho_parcial) << cor_steps;
    
        uint32_t valor[TOTAL_LEDS];
        for (int i = 0; i < TOTAL_LEDS; i++) {
            if (i < leds_full) {
                valor[i] = valor_full;
            } else if (i == leds_full && (led_parcial > 0.0)) {
                valor[i] = valor_parcial;
            } else {
                valor[i] = 0;
        }
    }
        for (int i = 0; i < TOTAL_LEDS; i++) {
            pio_sm_put_blocking(pio, sm_pio, valor[i]);
    }
        vTaskDelay(pdMS_TO_TICKS(200));
    }else{
        printf("Fila Vazia! ERRO! \n");
    }}
}
void gpio_irq_handler(uint gpio, uint32_t events)
{
    static absolute_time_t last_time_B = 0;
    absolute_time_t now = get_absolute_time();

    if (gpio == BUTTON_B) {
        if (absolute_time_diff_us(last_time_B, now) > 200000) {
            reset_usb_boot(0, 0);
            last_time_B = now;
        }

}}