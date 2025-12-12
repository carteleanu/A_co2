#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "wizchip_conf.h"
#include "wizchip_spi.h"
#include "socket.h"

#ifndef SF_BROADCAST
#define SF_BROADCAST (0x20)
#endif

#define UART_ID uart1
#define UART_TX_PIN 8
#define UART_RX_PIN 9

#define COMMAND_SOCKET 0
#define COMMAND_PORT 5000

#define BROADCAST_SOCKET 2
#define BROADCAST_PORT 5006

static wiz_NetInfo net_info = {
    .mac = {0x00, 0x08, 0xDC, 0x12, 0x34, 0x56},
    .ip  = {192, 168, 1, 101},
    .sn  = {255, 255, 255, 0},
    .gw  = {0, 0, 0, 0},  // PC is gateway when direct-connected
    .dns = {8, 8, 8, 8},

#if _WIZCHIP_ > W5500
    .ipmode = NETINFO_STATIC_ALL
#else
    .dhcp = NETINFO_STATIC
#endif
};

static void send_presence_broadcast(void)
{
    uint8_t broadcast_ip[4] = {255, 255, 255, 255};
    const char *msg = "W6100:CAR001:192.168.1.101:5000";

    if (getSn_SR(BROADCAST_SOCKET) == SOCK_CLOSED)
        socket(BROADCAST_SOCKET, Sn_MR_UDP, 0, SF_BROADCAST);

    sendto(BROADCAST_SOCKET, (uint8_t*)msg, strlen(msg), broadcast_ip, BROADCAST_PORT);
    printf("📡 Broadcast: %s\n", msg);
}

int main()
{
    stdio_init_all();
    sleep_ms(300); // allow boot settling (no USB wait!)

    uart_init(UART_ID, 115200);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    printf("\n=== W6100 Clean UDP Firmware Start ===\n");

    wizchip_spi_initialize();
    wizchip_cris_initialize();

    wizchip_reset();
    wizchip_initialize();
    wizchip_check();
    network_initialize(net_info);
    print_network_information(net_info);

    socket(COMMAND_SOCKET, Sn_MR_UDP, COMMAND_PORT, 0);
    printf(" UDP Command Socket Ready on Port %d\n", COMMAND_PORT);

    send_presence_broadcast();
    absolute_time_t last_broadcast = get_absolute_time();

    uint8_t rxbuf[256];
    uint8_t remote_ip[4];
    uint16_t remote_port;

    while (1)
    {
        // Periodic broadcast
        if (absolute_time_diff_us(last_broadcast, get_absolute_time()) > 10000000) // 10 seconds
        {
            send_presence_broadcast();
            last_broadcast = get_absolute_time();
        }

        // Non-blocking command receive
        if (getSn_RX_RSR(COMMAND_SOCKET) > 0)
        {
            int32_t len = recvfrom(COMMAND_SOCKET, rxbuf, sizeof(rxbuf)-1, remote_ip, &remote_port);
            if (len > 0)
            {
                // Ensure null-termination for string handling
                rxbuf[len] = 0;

                printf(" CMD from %d.%d.%d.%d:%d → %s\n",
                       remote_ip[0], remote_ip[1], remote_ip[2], remote_ip[3],
                       remote_port, rxbuf);
                
                if (len < sizeof(rxbuf) - 1) { // Check if there's room for '\n' before the null terminator
                    rxbuf[len] = '\n';    // Replace the null terminator with a newline
                    rxbuf[len + 1] = 0;   // Put the null terminator after the newline
                    len += 1;             // Update the effective length
                } else {
                    // If buffer is full, we still send but without appending '\n' easily.
                    // For simplicity, we assume the commands are short enough.
                }

                // Send the entire string, including the appended '\n', over UART
                uart_puts(UART_ID, (const char*)rxbuf);
                
                printf(" Forwarded to Teensy: %s", rxbuf); // rxbuf already includes the newline now
          
            }
        }
    }
}