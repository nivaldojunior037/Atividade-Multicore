# Comunicação Multi-core com Interrupção (RP2040)

Este projeto demonstra a utilização dos dois núcleos (cores) do microcontrolador **Raspberry Pi RP2040** (Pico, Pico W, etc.) para realizar tarefas de forma paralela.
Aqui tem quatro exemplos, sendo que um deles utiliza a **FIFO multi-core** para comunicação e tratamento de dados no Core 1 via **interrupção (IRQ)**.

##  Funcionalidade

O programa realiza as seguintes ações:

1.  **Core 0 (Principal):**
    * Lê um valor do **Conversor Analógico-Digital (ADC)** a cada 1 segundo (na GPIO26/ADC0).
    * Imprime o valor lido no terminal.
    * Envia o valor do ADC para o Core 1 através da **FIFO multi-core**.
2.  **Core 1 (Secundário):**
    * Aguarda passivamente.
    * Possui um *handler* de interrupção (`core1_interrupt_handler`) que é acionado **automaticamente** quando novos dados chegam na sua FIFO.
    * Ao ser interrompido, o *handler* lê o valor do ADC da FIFO.
    * Calcula a voltagem correspondente (considerando a referência de 3.3V e 12 bits de resolução).
    * Imprime o valor recebido e a voltagem calculada.

O principal diferencial é o uso da interrupção (`SIO_IRQ_PROC1`) no Core 1, o que o mantém em *sleep* (`tight_loop_contents()`) até que haja dados, garantindo uma execução mais eficiente e responsiva.




##  Saída Esperada

Ao executar, você deverá ver no terminal (via Serial USB) as mensagens sendo impressas pelos dois cores, demonstrando a comunicação assíncrona: