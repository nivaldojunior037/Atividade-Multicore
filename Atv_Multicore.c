#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/multicore.h"
#include "lib/aht20.h"
#include "lib/bh1750_light_sensor.h"
#include "lib/ssd1306.h"
#include "lib/font.h"

#define I2C_PORT i2c0               // i2c0 pinos 0 e 1, i2c1 pinos 2 e 3
#define I2C_SDA 0                   // 0 ou 2
#define I2C_SCL 1                   // 1 ou 3
#define I2C_PORT i2c0
#define SDA_PIN 0
#define SCL_PIN 1
#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define endereco 0x3C
#define ID_BH1750 1
#define ID_AHT20 2

const uint RED_PIN = 13;   // Pino para o LED vermelho
const uint GREEN_PIN = 11; // Pino para o LED verde
const uint BLUE_PIN = 12;  // Pino para o LED azul
AHT20_Data data;
ssd1306_t ssd;

// Estrutura para converter valores uint32_t para float e vice-versa
typedef union {
    float f;
    uint32_t u;
} conversor_float_t;

// Inicialização de pinos e periféricos
void setup() {

    gpio_init(RED_PIN);
    gpio_init(GREEN_PIN);
    gpio_init(BLUE_PIN);
    gpio_set_dir(RED_PIN, GPIO_OUT);
    gpio_set_dir(GREEN_PIN, GPIO_OUT);
    gpio_set_dir(BLUE_PIN, GPIO_OUT);
    gpio_put(RED_PIN, 0);
    gpio_put(GREEN_PIN, 0);
    gpio_put(BLUE_PIN, 0);

    stdio_init_all();
    sleep_ms(2000);

    // I2C do Display pode ser diferente dos sensores. Funcionando em 400Khz.
    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);                    
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);                    
    gpio_pull_up(I2C_SDA_DISP);                                        
    gpio_pull_up(I2C_SCL_DISP);                                        
                                                      
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT_DISP); 
    ssd1306_config(&ssd);                                              
    ssd1306_send_data(&ssd);                                           

    // Limpa o display. O display inicia com todos os pixels apagados.
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    // Inicializa o I2C0
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    bh1750_power_on(I2C_PORT);
}

// Função para processar os dados dos sensores no display
void process_display_data(uint32_t temperatura, uint32_t umidade, uint32_t valor_lux) {
    char temp_str[20];
    char umid_str[20];
    char lux_str[20];
    bool cor = true;
    conversor_float_t temp_display, umid_display;
    temp_display.u = temperatura;
    umid_display.u = umidade;

    ssd1306_fill(&ssd, false);
    ssd1306_rect(&ssd, 5, 5, 123, 59, cor, false);
    ssd1306_line(&ssd, 5, 20, 123, 20, cor);
    ssd1306_line(&ssd, 5, 40, 123, 40, cor);
    ssd1306_line(&ssd, 64, 5, 64, 40, cor);
    
    ssd1306_draw_string(&ssd, "Temp.", 10, 8);
    ssd1306_draw_string(&ssd, "Umid.", 67, 8);
    
    sprintf(temp_str, "%.1f", temp_display.f);
    sprintf(umid_str, "%.1f", umid_display.f);
    sprintf(lux_str, "%d", valor_lux);
    ssd1306_draw_string(&ssd, temp_str, 10, 31);
    ssd1306_draw_string(&ssd, umid_str, 67, 31);
    ssd1306_draw_string(&ssd, "Lux:", 10, 50);
    ssd1306_draw_string(&ssd, lux_str, 40, 50);
    ssd1306_send_data(&ssd);

    // Atualiza os LEDs baseado na temperatura convertida
    if(temp_display.f > 45.0) {
        gpio_put(RED_PIN, 1);
        gpio_put(GREEN_PIN, 0);
        gpio_put(BLUE_PIN, 0);
    }
    else if(temp_display.f < 20.0) {
        gpio_put(GREEN_PIN, 0);
        gpio_put(RED_PIN, 1);
        gpio_put(BLUE_PIN, 1);
    }
    else {
        gpio_put(BLUE_PIN, 0);
        gpio_put(RED_PIN, 0);
        gpio_put(GREEN_PIN, 1);
    }
}

// Função de ativação do núcleo 1 do RP2040
void core1_entry() {
    uint32_t temperatura = 0;
    uint32_t umidade = 0;
    uint32_t valor_lux = 0;

    while (true) {
        // Core 1 lê o sensor BH1750
        uint16_t lux = bh1750_read_measurement(I2C_PORT);
        valor_lux = lux;
        printf("Leitura do BH1750 (core 1): %d lux\n", lux);
        
        // Verifica se há dados do sensor de temperatura disponíveis
        if (multicore_fifo_rvalid()) {
            uint32_t id_sensor = multicore_fifo_pop_blocking();
            if (id_sensor == ID_AHT20) {
                temperatura = multicore_fifo_pop_blocking();
                umidade = multicore_fifo_pop_blocking();
                printf("Dados do AHT20 recebidos\n");
            }
        }

        // Atualiza o display e os LEDs com os dados mais recentes
        process_display_data(temperatura, umidade, valor_lux);
        
        sleep_ms(500);
    }
}

int main() {
    setup();
    // Inicialização do core 1 do RP2040
    multicore_launch_core1(core1_entry);
    
    conversor_float_t temp_convert, umid_convert;

    while (true) {
        // O core0 lê o sensor AHT20
        aht20_read(I2C_PORT, &data);
        temp_convert.f = data.temperature;
        umid_convert.f = data.humidity;
        printf("Leitura do AHT20 (core 0): %.2f °C | %.2f %%\n", data.temperature, data.humidity);
        multicore_fifo_push_blocking(ID_AHT20); 
        multicore_fifo_push_blocking(temp_convert.u); 
        multicore_fifo_push_blocking(umid_convert.u); 
        sleep_ms(500);
    }
}
