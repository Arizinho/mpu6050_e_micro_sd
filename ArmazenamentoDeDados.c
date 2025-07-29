#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "pico/binary_info.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

#include "ssd1306.h"
#include "font.h"
#include "ff.h"
#include "diskio.h"
#include "f_util.h"
#include "hw_config.h"
#include "my_debug.h"
#include "sd_card.h"

// Definição dos pinos I2C para o MPU6050
#define I2C_PORT i2c0                 // I2C0 usa pinos 0 e 1
#define I2C_SDA 0
#define I2C_SCL 1

// Definição dos pinos I2C para o display OLED
#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define ENDERECO_DISP 0x3C            // Endereço I2C do display

// Definição dos pinos GPIO
#define botaoA 5
#define botaoB 6
#define LED_RED 13
#define LED_GREEN 11
#define LED_BLUE 12
#define BUZZER 10

bool captura_on = false; // Variável para controlar a captura de dados
bool ativa_montagem = false; // Variável para controlar a montagem/desmontagem do cartão SD
bool cartao_montado = false; // Variável para indicar se o cartão SD está montado
bool leitura_ativa = false; // Variável para indicar se a leitura está ativa
bool ocorreu_desmontagem = false; // Variável para se a primeira desmontagem ocorreu
bool cor = true;

//Protótipos das funções
void init_gpios();
void apaga_leds();

ssd1306_t ssd;

// Endereço padrão do MPU6050
static int addr = 0x68;

// Função para resetar e inicializar o MPU6050
static void mpu6050_reset()
{
    // Dois bytes para reset: primeiro o registrador, segundo o dado
    uint8_t buf[] = {0x6B, 0x80};
    i2c_write_blocking(I2C_PORT, addr, buf, 2, false);
    sleep_ms(100); // Aguarda reset e estabilização

    // Sai do modo sleep (registrador 0x6B, valor 0x00)
    buf[1] = 0x00;
    i2c_write_blocking(I2C_PORT, addr, buf, 2, false);
    sleep_ms(10); // Aguarda estabilização após acordar
}

// Função para ler dados crus do acelerômetro, giroscópio e temperatura
static void mpu6050_read_raw(int16_t accel[3], int16_t gyro[3], int16_t *temp)
{
    uint8_t buffer[6];

    // Lê aceleração a partir do registrador 0x3B (6 bytes)
    uint8_t val = 0x3B;
    i2c_write_blocking(I2C_PORT, addr, &val, 1, true);
    i2c_read_blocking(I2C_PORT, addr, buffer, 6, false);

    for (int i = 0; i < 3; i++)
    {
        accel[i] = (buffer[i * 2] << 8) | buffer[(i * 2) + 1];
    }

    // Lê giroscópio a partir do registrador 0x43 (6 bytes)
    val = 0x43;
    i2c_write_blocking(I2C_PORT, addr, &val, 1, true);
    i2c_read_blocking(I2C_PORT, addr, buffer, 6, false);

    for (int i = 0; i < 3; i++)
    {
        gyro[i] = (buffer[i * 2] << 8) | buffer[(i * 2) + 1];
    }

    // Lê temperatura a partir do registrador 0x41 (2 bytes)
    val = 0x41;
    i2c_write_blocking(I2C_PORT, addr, &val, 1, true);
    i2c_read_blocking(I2C_PORT, addr, buffer, 2, false);

    *temp = (buffer[0] << 8) | buffer[1];
}

// Nome do arquivo onde os dados serão salvos
static char filename[20] = "mpu6050_data.csv";

static sd_card_t *sd_get_by_name(const char *const name)
{
    for (size_t i = 0; i < sd_get_num(); ++i)
        if (0 == strcmp(sd_get_by_num(i)->pcName, name))
            return sd_get_by_num(i);
    DBG_PRINTF("%s: unknown name %s\n", __func__, name);
    return NULL;
}
static FATFS *sd_get_fs_by_name(const char *name)
{
    for (size_t i = 0; i < sd_get_num(); ++i)
        if (0 == strcmp(sd_get_by_num(i)->pcName, name))
            return &sd_get_by_num(i)->fatfs;
    DBG_PRINTF("%s: unknown name %s\n", __func__, name);
    return NULL;
}

// Função para montar o cartão SD
static void run_mount()
{
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = sd_get_by_num(0)->pcName;
    FATFS *p_fs = sd_get_fs_by_name(arg1);

    // Caso ocorra erro na montagem do cartão SD
    if (!p_fs)
    {
        // Exibe mensagem de erro no display OLED e pisca os LEDs em roxo
        ssd1306_fill(&ssd, !cor);
        ssd1306_rect(&ssd, 1, 1, 124, 62, cor, !cor);  
        ssd1306_draw_string(&ssd, "Falha ao", 4, 4);
        ssd1306_draw_string(&ssd, "montar SD", 10, 14);
        ssd1306_send_data(&ssd);
        apaga_leds();
        for(int i=1; i <= 10; i++){
            gpio_put(LED_RED, i % 2);
            gpio_put(LED_BLUE, i % 2);
            sleep_ms(500);
        }
        return;
    }
    FRESULT fr = f_mount(p_fs, arg1, 1);

    // Caso ocorra erro na montagem do cartão SD
    if (FR_OK != fr)
    {
        // Exibe mensagem de erro no display OLED e pisca os LEDs em roxo
        ssd1306_fill(&ssd, !cor);
        ssd1306_rect(&ssd, 1, 1, 124, 62, cor, !cor); 
        ssd1306_draw_string(&ssd, "Falha ao", 4, 4);
        ssd1306_draw_string(&ssd, "montar SD", 10, 14);
        ssd1306_send_data(&ssd);
        apaga_leds();
        for(int i=1; i <= 10; i++){
            gpio_put(LED_RED, i % 2);
            gpio_put(LED_BLUE, i % 2);
            sleep_ms(500);
        }
        return;
    }
    sd_card_t *pSD = sd_get_by_name(arg1);
    myASSERT(pSD);
    pSD->mounted = true;

    // Caso o cartão SD seja montado envia mensagem de sucesso no display OLED
    ssd1306_fill(&ssd, !cor); 
    ssd1306_rect(&ssd, 1, 1, 124, 62, cor, !cor); 
    ssd1306_draw_string(&ssd, "SUCESS:", 4, 4);
    ssd1306_draw_string(&ssd, "SD montado", 10, 14);
    ssd1306_send_data(&ssd);
    cartao_montado = true; // Atualiza o estado do cartão SD
    sleep_ms(2000);
}

// Função para desmontar o cartão SD
static void run_unmount()
{
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = sd_get_by_num(0)->pcName;
    FATFS *p_fs = sd_get_fs_by_name(arg1);

    // Caso ocorra erro na desmontagem do cartão SD
    if (!p_fs)
    {
        // Exibe mensagem de erro no display OLED e pisca os LEDs em roxo
        ssd1306_fill(&ssd, !cor); 
        ssd1306_rect(&ssd, 1, 1, 124, 62, cor, !cor); 
        ssd1306_draw_string(&ssd, "Falha ao", 4, 4);
        ssd1306_draw_string(&ssd, "desmontar SD", 10, 14);
        ssd1306_send_data(&ssd);
        apaga_leds();
        for(int i=1; i <= 10; i++){
            gpio_put(LED_RED, i % 2);
            gpio_put(LED_BLUE, i % 2);
            sleep_ms(500);
        }
        return;
    }
    FRESULT fr = f_unmount(arg1);

    // Caso ocorra erro na desmontagem do cartão SD
    if (FR_OK != fr)
    {
        // Exibe mensagem de erro no display OLED e pisca os LEDs em roxo
        ssd1306_fill(&ssd, !cor); 
        ssd1306_rect(&ssd, 1, 1, 124, 62, cor, !cor); 
        ssd1306_draw_string(&ssd, "Falha ao", 4, 4);
        ssd1306_draw_string(&ssd, "desmontar SD", 10, 14);
        ssd1306_send_data(&ssd);
        apaga_leds();
        for(int i=1; i <= 10; i++){
            gpio_put(LED_RED, i % 2);
            gpio_put(LED_BLUE, i % 2);
            sleep_ms(500);
        }
        return;
    }
    sd_card_t *pSD = sd_get_by_name(arg1);
    myASSERT(pSD);
    pSD->mounted = false;
    pSD->m_Status |= STA_NOINIT; // in case medium is removed

    // Caso o cartão SD seja desmontado envia mensagem de sucesso no display OLED
    ssd1306_fill(&ssd, !cor); 
    ssd1306_rect(&ssd, 1, 1, 124, 62, cor, !cor); 
    ssd1306_draw_string(&ssd, "SUCESS:", 4, 4);
    ssd1306_draw_string(&ssd, "SD desmontado", 10, 14);
    ocorreu_desmontagem = true; // Marca que a primeira desmontagem ocorreu
    ssd1306_send_data(&ssd);
    cartao_montado = false; // Atualiza o estado do cartão SD
    sleep_ms(2000);
}

// Função para capturar dados do MPU6050 e salvar no arquivo em formato csv
void capture_data_and_save()
{
    // Dá um beep para indicar que a captura começou
    pwm_set_gpio_level(BUZZER, 1000); 
    sleep_ms(200);
    pwm_set_gpio_level(BUZZER, 0); 

    // Salva o tempo inicial para calcular o tempo de captura
    uint32_t tempo_inicial = to_ms_since_boot(get_absolute_time());

    // Indica que a leitura está ativa
    leitura_ativa = true;

    // Variáveis para armazenar os dados do MPU6050
    int16_t aceleracao[3], gyro[3], temp;

    // Buffers para armazenar os dados
    char buffer[100] = "numero_amostra;accel_x;accel_y;accel_z;giro_x;giro_y;giro_z\n", buffer2[10];

    FIL file;
    FRESULT res = f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);

    // Envia mensagem de erro no display OLED e liga LED vermelho se houver falha na escrita do arquivo
    if (res != FR_OK)
    {
        ssd1306_fill(&ssd, !cor); 
        ssd1306_rect(&ssd, 1, 1, 124, 62, cor, !cor);  
        ssd1306_draw_string(&ssd, "Falha", 4, 4);
        ssd1306_draw_string(&ssd, "na escrita", 4, 14);
        ssd1306_send_data(&ssd);
        gpio_put(LED_RED, 1);
        sleep_ms(2000);
        leitura_ativa = false;
        captura_on = false;
        return;
    }
    UINT bw;
    res = f_write(&file, buffer, strlen(buffer), &bw);

    // Envia mensagem de erro no display OLED e liga LED vermelho se houver falha na escrita do arquivo
    if (res != FR_OK)
        {
            ssd1306_fill(&ssd, !cor); 
            ssd1306_rect(&ssd, 1, 1, 124, 62, cor, !cor);  
            ssd1306_draw_string(&ssd, "Falha", 4, 4);
            ssd1306_draw_string(&ssd, "na escrita", 4, 14);
            ssd1306_send_data(&ssd);
            gpio_put(LED_RED, 1);
            sleep_ms(2000);
            f_close(&file);
            leitura_ativa = false;
            captura_on = false;
            return;
        }

    int i = 1;
    uint32_t time_since_beginning;
    
    while (captura_on)
    {
        mpu6050_read_raw(aceleracao, gyro, &temp);

        // Conversão para unidade de 'g'
        float ax = aceleracao[0] / 16384.0f;
        float ay = aceleracao[1] / 16384.0f;
        float az = aceleracao[2] / 16384.0f;

        // Calcula o tempo desde o início da captura em segundos
        time_since_beginning = (to_ms_since_boot(get_absolute_time()) - tempo_inicial) / 1000;

        // Formata os dados para o buffer
        snprintf(buffer, sizeof(buffer), "%d;%.2f;%.2f;%.2f;%d;%d;%d\n", i, ax, ay, az, gyro[0], gyro[1], gyro[2]);
        snprintf(buffer2, sizeof(buffer2), "%d s", time_since_beginning);
        
        // Pisca LED azul a cada segundo
        gpio_put(LED_BLUE, !(time_since_beginning % 2));

        // Atualiza o display OLED com tempo
        ssd1306_rect(&ssd, 14, 4, 106, 10, !cor, cor); 
        ssd1306_draw_string(&ssd, buffer2, 30, 14);
        ssd1306_send_data(&ssd);
        i++;

        // Escreve os dados no cartão micro SD
        res = f_write(&file, buffer, strlen(buffer), &bw);

        // Envia mensagem de erro no display OLED e liga LED vermelho se houver falha na escrita do arquivo
        if (res != FR_OK)
        {
            apaga_leds();
            ssd1306_fill(&ssd, !cor); 
            ssd1306_rect(&ssd, 1, 1, 124, 62, cor, !cor);  
            ssd1306_draw_string(&ssd, "Falha", 4, 4);
            ssd1306_draw_string(&ssd, "na escrita", 4, 14);
            ssd1306_send_data(&ssd);
            gpio_put(LED_RED, 1);
            sleep_ms(2000);
            f_close(&file);
            leitura_ativa = false;
            captura_on = false;
            return;
        }
        sleep_ms(200);
    }

    // Finaliza a captura, envia mensagem de sucesso no display OLED e dá um duplo beep indicando que a escrita foi concluída
    f_close(&file);
    ssd1306_fill(&ssd, !cor); 
    ssd1306_rect(&ssd, 1, 1, 124, 62, cor, !cor); 
    ssd1306_draw_string(&ssd, "Escrita", 4, 4);
    ssd1306_draw_string(&ssd, "finalizada", 35, 14);
    ssd1306_send_data(&ssd);
    pwm_set_gpio_level(BUZZER, 1000); 
    sleep_ms(200);
    pwm_set_gpio_level(BUZZER, 0); 
    sleep_ms(200);
    pwm_set_gpio_level(BUZZER, 1000); 
    sleep_ms(200);
    pwm_set_gpio_level(BUZZER, 0); 
    sleep_ms(1400);
    leitura_ativa = false;  // Atualiza o estado da leitura
}

void gpio_irq_handler(uint gpio, uint32_t events)
{
    // Salva o tempo atual para evitar múltiplas leituras rápidas
    static uint32_t last_time = 0;

    // Verifica se o botão A foi pressionado e se o cartão SD está montado
    // e se o tempo desde a última leitura é maior que 200 ms
    if (gpio == botaoA && cartao_montado && (to_ms_since_boot(get_absolute_time()) - last_time) > 200) 
    {
        last_time = to_ms_since_boot(get_absolute_time());  // Atualiza o tempo da última leitura
        captura_on = !captura_on; // Alterna o estado da captura
    }

    // Verifica se o botão B foi pressionado e se a leitura não está ativa
    // e se o tempo desde a última leitura é maior que 200 ms
    else if (gpio == botaoB && !leitura_ativa && (to_ms_since_boot(get_absolute_time()) - last_time) > 200)
    {
        last_time = to_ms_since_boot(get_absolute_time());  // Atualiza o tempo da última leitura
        ativa_montagem = true; // Ativa a montagem/desmontagem do cartão SD
    }
}

int main()
{
    // Inicializa os pinos GPIO
    init_gpios();

    // Configura os pinos de interrupção para os botões
    gpio_set_irq_enabled_with_callback(botaoA, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(botaoB, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    stdio_init_all();
    sleep_ms(1000);

    // Inicializa o display OLED
    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);

    ssd1306_init(&ssd, WIDTH, HEIGHT, false, ENDERECO_DISP, I2C_PORT_DISP);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    // Inicializa o MPU6050
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    bi_decl(bi_2pins_with_func(I2C_SDA, I2C_SCL, GPIO_FUNC_I2C));
    mpu6050_reset();

    while (true)
    {
        // Apaga os LEDs e limpa o display OLED
        apaga_leds();
        ssd1306_fill(&ssd, !cor);  
        ssd1306_rect(&ssd, 1, 1, 124, 62, cor, !cor);                                      
        
        // Verifica se o cartão SD não está montado
        if (!cartao_montado)
        {
            // Se não foi solicitada montagem, exibe instruções no display OLED
            if (!ativa_montagem)
            {
                ssd1306_draw_string(&ssd, "Press B:", 4, 4);
                ssd1306_draw_string(&ssd, "Montar SD", 10, 14);

                // Se a desmontagem já ocorreu, exibe mensagem de remoção do cartão SD
                if (ocorreu_desmontagem)
                {
                    ssd1306_line(&ssd, 1, 30, 123, 30, cor); 
                    ssd1306_draw_string(&ssd, "Cartao pode", 4, 33);
                    ssd1306_draw_string(&ssd, "ser removido", 14, 43);
                } 
                ssd1306_send_data(&ssd);
            }

            // Se foi solicitada montagem do cartão SD
            else{
                ssd1306_draw_string(&ssd, "Montando SD", 4, 4);
                ssd1306_send_data(&ssd);

                // Liga os LEDs em amarelo para indicar montagem
                gpio_put(LED_RED, 1);
                gpio_put(LED_GREEN, 1);
                run_mount();
                ativa_montagem = false;
            }

        }

        // Se o cartão SD está montado
        else{

            // Se a desmontagem foi solicitada, exibe mensagem de desmontagem
            if(ativa_montagem)
            {
                ssd1306_draw_string(&ssd, "Desmontando SD ", 4, 4);
                ssd1306_send_data(&ssd);

                // Liga os LEDs em amarelo para indicar desmontagem
                gpio_put(LED_RED, 1);
                gpio_put(LED_GREEN, 1);
                run_unmount();
                ativa_montagem = false;
            }

            else {
                // Se a captura não está ativa, exibe opções de captura e desmontagem
                if(!captura_on){
                    gpio_put(LED_GREEN, 1);
                    ssd1306_draw_string(&ssd, "Press A:", 4, 4);
                    ssd1306_draw_string(&ssd, "Capturar dados", 10, 14);
                    ssd1306_line(&ssd, 1, 30, 123, 30, cor); 
                    ssd1306_draw_string(&ssd, "Press B:", 4, 33);
                    ssd1306_draw_string(&ssd, "Desmontar SD", 10, 43);
                    ssd1306_send_data(&ssd);
                }

                // Se a captura está ativa, exibe mensagem de gravação e inicia a captura
                else{
                    ssd1306_draw_string(&ssd, "Gravando...", 4, 4);
                    ssd1306_line(&ssd, 1, 30, 123, 30, cor); 
                    ssd1306_draw_string(&ssd, "Press A:", 4, 33);
                    ssd1306_draw_string(&ssd, "Parar captura", 10, 43);
                    ssd1306_send_data(&ssd);
                    capture_data_and_save();
                }
            }
        }
        sleep_ms(100);
    }
    return 0;
}

// Função para inicializar os pinos GPIO
void init_gpios()
{
    // Inicializa os pinos GPIO
    gpio_init(botaoA);
    gpio_set_dir(botaoA, GPIO_IN);
    gpio_pull_up(botaoA);

    gpio_init(botaoB);
    gpio_set_dir(botaoB, GPIO_IN);
    gpio_pull_up(botaoB);

    gpio_init(LED_RED);
    gpio_set_dir(LED_RED, GPIO_OUT);
    gpio_put(LED_RED, 0);

    gpio_init(LED_GREEN);
    gpio_set_dir(LED_GREEN, GPIO_OUT);
    gpio_put(LED_GREEN, 0);

    gpio_init(LED_BLUE);
    gpio_set_dir(LED_BLUE, GPIO_OUT);
    gpio_put(LED_BLUE, 0);

    gpio_set_function(BUZZER, GPIO_FUNC_PWM);
    pwm_set_clkdiv(5, 125.0); 
    pwm_set_wrap(5, 1999); 
    pwm_set_gpio_level(BUZZER, 0); 
    pwm_set_enabled(5, true);
}

// Função para apagar os LEDs
void apaga_leds()
{
    gpio_put(LED_RED, 0);
    gpio_put(LED_GREEN, 0);
    gpio_put(LED_BLUE, 0);
}