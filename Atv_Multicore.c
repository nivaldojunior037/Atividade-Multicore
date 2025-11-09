#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/multicore.h"
#include "lib/aht20.h"
#include "lib/bh1750_light_sensor.h"
#include "lib/ssd1306.h"
#include "lib/font.h"

// ======= Configuração de I2C e pinos =======
#define I2C_PORT        i2c0        // Sensores (AHT20 e BH1750) no core 0
#define I2C_SDA         0
#define I2C_SCL         1

#define I2C_PORT_DISP   i2c1        // Display no core 1
#define I2C_SDA_DISP    14
#define I2C_SCL_DISP    15
#define ENDERECO_OLED   0x3C

// ======= IDs de pacote =======
#define ID_PACKET       0xAA55

// ======= Pinos de LED =======
const uint RED_PIN   = 13;
const uint GREEN_PIN = 11;
const uint BLUE_PIN  = 12;

// ======= Globais =======
AHT20_Data data;
ssd1306_t ssd;

// Conversor para enviar float via FIFO
typedef union {
    float f;
    uint32_t u;
} conversor_float_t;

// ======= Setup de pinos e periféricos =======
void setup() {
    // LEDs
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

    // I2C do Display (i2c1)
    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);

    ssd1306_init(&ssd, WIDTH, HEIGHT, false, ENDERECO_OLED, I2C_PORT_DISP);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    // Limpa o display
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    // I2C dos sensores (i2c0)
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Liga BH1750
    bh1750_power_on(I2C_PORT);
}

// ======= Renderização no display + lógica dos LEDs (core 1) =======
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
    sprintf(lux_str, "%lu", (unsigned long)valor_lux);

    ssd1306_draw_string(&ssd, temp_str, 10, 31);
    ssd1306_draw_string(&ssd, umid_str, 67, 31);
    ssd1306_draw_string(&ssd, "Lux:", 10, 50);
    ssd1306_draw_string(&ssd, lux_str, 40, 50);
    ssd1306_send_data(&ssd);

    // LEDs por temperatura
    if (temp_display.f > 45.0f) {
        gpio_put(RED_PIN, 1);
        gpio_put(GREEN_PIN, 0);
        gpio_put(BLUE_PIN, 0);
    } else if (temp_display.f < 20.0f) {
        gpio_put(GREEN_PIN, 0);
        gpio_put(RED_PIN, 1);
        gpio_put(BLUE_PIN, 1);
    } else {
        gpio_put(BLUE_PIN, 0);
        gpio_put(RED_PIN, 0);
        gpio_put(GREEN_PIN, 1);
    }
}

// ======= Núcleo 1: só recebe e publica =======
void core1_entry() {
    while (1) {
        // Espera um pacote completo: ID + T + U + LUX
        uint32_t id = multicore_fifo_pop_blocking();
        if (id == ID_PACKET) {
            uint32_t temperatura = multicore_fifo_pop_blocking();
            uint32_t umidade     = multicore_fifo_pop_blocking();
            uint32_t valor_lux   = multicore_fifo_pop_blocking();

            // Debug opcional
            // printf("Core1: pkt T=%lu U=%lu LUX=%lu\n",
            //        (unsigned long)temperatura, (unsigned long)umidade, (unsigned long)valor_lux);

            process_display_data(temperatura, umidade, valor_lux);
        } else {
            // Descarte se ID inesperado
            // (Opcionalmente: drenar/realinhar)
        }
    }
}

// ======= Núcleo 0: lê AHT20 + BH1750 e envia =======
int main() {
    setup();

    // Sobe o core 1
    multicore_launch_core1(core1_entry);

    conversor_float_t temp_convert, umid_convert;

    while (1) {
        // Leitura AHT20 (I2C0)
        aht20_read(I2C_PORT, &data);
        temp_convert.f = data.temperature;
        umid_convert.f = data.humidity;

        // Leitura BH1750 (I2C0)
        uint16_t lux = bh1750_read_measurement(I2C_PORT);

        // Debug
        // printf("Core0: AHT20 %.2f C / %.2f %% | BH1750 %u lux\n",
        //        data.temperature, data.humidity, lux);

        // Envia pacote completo para o core 1
        multicore_fifo_push_blocking(ID_PACKET);
        multicore_fifo_push_blocking(temp_convert.u);
        multicore_fifo_push_blocking(umid_convert.u);
        multicore_fifo_push_blocking((uint32_t)lux);

        sleep_ms(500);
    }
}
