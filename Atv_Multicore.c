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

// Função de ativação do núcleo 1 do RP2040
void core1_entry() {
    while (true)
    {
        // Organização para enviar os dados dos sensores sem colidir dados:
        // O primeiro valor da FIFO será um identificador que determina qual sensor realizou a última leitura
        uint32_t id_sensor = multicore_fifo_pop_blocking(); 

        // Caso seja o BH1750, é lido apenas o valor de lux enviado pelo core 0
        if(id_sensor == ID_BH1750){
            uint32_t valor_lux = multicore_fifo_pop_blocking();
            // Comunicação de recebimento no terminal
            printf("Leitura recebida do núcleo 0 \n\nSensor BH1750 \nLeitura: %d\n\n\n", valor_lux);
        } 
        
        // Caso seja o AHT20, são lidos os valores de temperatura e umidade e convertidos para float 
        else if (id_sensor == ID_AHT20){
            uint32_t temperatura = multicore_fifo_pop_blocking();
            uint32_t umidade = multicore_fifo_pop_blocking();

            conversor_float_t temp_convert, umid_convert;
            temp_convert.u = temperatura; 
            umid_convert.u = umidade;
            // Comunicação de recebimento no terminal
            printf("Leitura recebida do núcleo 0. \n\n Sensor AHT20 \nLeitura: %.2f ºC, %.2f %\n\n\n", temp_convert.f, umid_convert.f);
        } else {
            printf("Identificação de sensor inválida. \n");
        }
    }
}


int main() {

    setup();
    // Inicialização do core 1 do RP2040
    multicore_launch_core1(core1_entry);
    conversor_float_t temp_convert, umid_convert;

    while (true)
    {
        // O BH1750 realiza a leitura e manda, em ordem, seu identificador e o valor lido de lux; 
        uint16_t lux = bh1750_read_measurement(I2C_PORT);
        multicore_fifo_push_blocking(ID_BH1750);
        multicore_fifo_push_blocking(lux);
        sleep_ms(500);

        // Em seguida, o AHT20 realiza a leitura e manda, também em ordem, seu identificador, a medição de temperatura e de umidade; 
        aht20_read(I2C_PORT, &data);
        temp_convert.f = data.temperature;
        umid_convert.f = data.humidity;
        multicore_fifo_push_blocking(ID_AHT20); 
        multicore_fifo_push_blocking(temp_convert.u); 
        multicore_fifo_push_blocking(umid_convert.u); 
        sleep_ms(500);
    }
}
