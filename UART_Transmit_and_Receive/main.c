#include "cyhal.h"
#include "cybsp.h"

#define BUF_SIZE 24

int main(void)
{
	cy_rslt_t rslt;
	cyhal_uart_t uart_obj;
	uint32_t actualbaud;

	uint8_t TX_buffer[BUF_SIZE] = { 0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0b10101010, 0b01010101 };
	size_t tx_length = BUF_SIZE;

const cyhal_uart_cfg_t uart_config =
{
.data_bits = 8,
.stop_bits = 2,
.parity = CYHAL_UART_PARITY_NONE,
.rx_buffer = TX_buffer,
.rx_buffer_size = BUF_SIZE
};

/* Initialize board */
rslt = cybsp_init();

rslt = cyhal_uart_init(&uart_obj, CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, NC, NC, NULL, &uart_config);
rslt = cyhal_uart_set_baud(&uart_obj, 250000, &actualbaud);

__enable_irq();

for (;;)
{
cyhal_uart_write(&uart_obj, (void*)TX_buffer, &tx_length);
cyhal_system_delay_ms(1000);
}
}
