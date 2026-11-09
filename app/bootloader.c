#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "stm32f4xx.h"
#include "stm32f4xx_ll_tim.h"
#include "stm32f4xx_ll_usart.h"

#include "board.h"
#include "bl_usart.h"
#include "ringbuffer.h"
#include "crc16.h"
#include "crc32.h"
#include "time_delay.h"
#include "stm32_flash.h"
#include "key.h"
#include "led.h"
#include "magic_header.h"
#include "utils.h"

#define LOG_TAG    "bootloader"
#define LOG_LVL    ELOG_LVL_INFO
#include "elog.h"

#define APP_ADDRESS 0x08010000
#define APP_SIZE 0x70000

#define PACKET_HEADER_REQUEST               0xAA
#define PACKET_HEADER_RESPONSE              0x55

#define PACKET_HEADER_SIZE                  1
#define PACKET_OPCODE_SIZE                  1
#define PACKET_LENGTH_SIZE                  2
#define PACKET_CRC_SIZE                     2
#define PACKET_HEADER_OFFSET                0
#define PACKET_OPCODE_OFFSET                (PACKET_HEADER_OFFSET + PACKET_HEADER_SIZE)
#define PACKET_LENGTH_OFFSET                (PACKET_OPCODE_OFFSET + PACKET_OPCODE_SIZE)
#define PACKET_PAYLOAD_OFFSET               (PACKET_LENGTH_OFFSET + PACKET_LENGTH_SIZE)
#define PACKET_MIN_SIZE                     (PACKET_HEADER_SIZE + PACKET_OPCODE_SIZE + PACKET_LENGTH_SIZE + PACKET_CRC_SIZE)

#define ADDR_PARAM_LENGTH                   4
#define SIZE_PARAM_LENGTH                   4
#define ADDR_SIZE_PARAM_LENGTH              8
#define ADDR_SIZE_CRC_PARAM_LENGTH          12
#define PROGRAM_PARAM_HEADER_SIZE           8

#define BL_TRAP_BOOT_TIME                   3000
#define BL_FLASH_BASE_ADDR                  0x08000000
#define BL_FLASH_SIZE                       (48 * 1024)
#define BL_VERSION                          "0.0.1"
#define BL_PAYLOAD_BUFFER_SIZE              (4096 + ADDR_SIZE_PARAM_LENGTH)
#define BL_RX_PACKET_BUFFER_SIZE            (PACKET_PAYLOAD_OFFSET + BL_PAYLOAD_BUFFER_SIZE + PACKET_CRC_SIZE)
#define BL_RINGBUFFER_SIZE                  (5 * 1024)
#define BL_BYTE_RESET_TIME_MS               20

// TX packet format:
// HEADER OPCODE LEN DATA CRC16
// state exchange parse:
// HEADER->OPCODE->LEN->PAYLOADZ->CRC16
typedef enum
{
    PACKET_STATE_HEADER = 0,
    PACKET_STATE_OPCODE,
    PACKET_STATE_LEN,
    PACKET_STATE_PAYLOAD,
    PACKET_STATE_CRC16,
} packet_state_machine_t;

// INQUERY OPCODE
// VERSION MTU
typedef enum
{
    SUBCODE_INQUERY_VERSION,
    SUBCODE_INQUERY_MTU,
} subcode_inquery_t;

// OPCODE
typedef enum
{
    PACKET_OPCODE_NONE = 0,
    PACKET_OPCODE_RESET = 0x21,
    PACKET_OPCODE_BOOT = 0x22,
    PACKET_OPCODE_INQUERY = 0x01,
    PACKET_OPCODE_ERASE = 0x81,
    PACKET_OPCODE_PROGRAM = 0x82,
    PACKET_OPCODE_VERIFY = 0x83,
    PACKET_OPCODE_READ = 0x80,
} packet_opcode_t;

// RX packet format:
// HEADER OPCODE ERRCODE LEN DATA CRC32
// ERRCODE
typedef enum
{
    PACKET_ERRCODE_OK = 0,
    PACKET_ERRCODE_OPCODE,
    PACKET_ERRCODE_OVERFLOW,
    PACKET_ERRCODE_TIMEOUT,
    PACKET_ERRCODE_FORMANT,
    PACKET_ERRCODE_VERIFY,
    PACKET_ERRCODE_PARAM,
    PACKET_ERRCODE_UNKNOW = 0xff,
} packet_errcode_t;

static uint8_t rb_buffer[BL_RINGBUFFER_SIZE];
static rb_t rb;

static packet_state_machine_t packet_buffer_state = PACKET_STATE_HEADER;
static uint8_t packet_buffer[BL_RX_PACKET_BUFFER_SIZE];
static uint16_t packet_buffer_index = 0;
static uint16_t packet_payload_len = 0;

static bool application_valid_check(void)
{
    uint32_t addr = magic_header_get_addr();
    uint32_t size = magic_header_get_size();
    uint32_t crc = magic_header_get_crc();
    uint32_t ccrc = crc32((const unsigned char *)addr, size);
    if (crc != ccrc)
    {
        log_e("fireware crc invalid, expect:%04X, recv:%04X", crc, ccrc);
        return false;
    }
    return true;
}

static bool magic_header_trap_app_check(void)
{
    if (!magic_header_valid_check())
    {
        log_e("magic header invalid");
        return false;
    }
    if (!application_valid_check())
    {
        log_e("fireware invalid");
        return false;
    }
    return true;
}

static bool bl_rx_packet_parse(uint8_t *data, uint16_t size)
{
    while (size--)
    {
        static uint64_t now, last_time;
        now = time_get_ms();
        if (now - last_time > BL_BYTE_RESET_TIME_MS)
        {
            if (packet_buffer_state != PACKET_STATE_HEADER)
                log_e("rx timeout, state reset");
            packet_buffer_index = 0;
            packet_buffer_state = PACKET_STATE_HEADER;
        }
        last_time = now;

        log_v("recv %02X", *data);
        packet_buffer[packet_buffer_index++] = *data++;
        switch (packet_buffer_state)
        {
        case PACKET_STATE_HEADER:
            if (packet_buffer[PACKET_HEADER_OFFSET] == PACKET_HEADER_REQUEST)
            {
                log_d("header ok:%02X", packet_buffer[PACKET_HEADER_OFFSET]);
                packet_buffer_state = PACKET_STATE_OPCODE;
            }
            else
            {
                packet_buffer_index = 0;
                packet_buffer_state = PACKET_STATE_HEADER;
            }
            break;

        case PACKET_STATE_OPCODE:
            if (
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_NONE ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_INQUERY ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_RESET ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_BOOT ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_ERASE ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_PROGRAM ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_VERIFY ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_READ)
            {
                log_d("opcode ok:%02X", packet_buffer[PACKET_OPCODE_OFFSET]);
                packet_buffer_state = PACKET_STATE_LEN;
            }
            else
            {
                packet_buffer_index = 0;
                packet_buffer_state = PACKET_STATE_HEADER;
            }
            break;

        case PACKET_STATE_LEN:
            if (packet_buffer_index == PACKET_PAYLOAD_OFFSET)
            {
                packet_payload_len = get_u16(packet_buffer + PACKET_LENGTH_OFFSET);
                if (packet_payload_len + 4 <= BL_RX_PACKET_BUFFER_SIZE)
                {
                    log_d("len ok: %d", packet_payload_len);
                    if (packet_payload_len > 0)
                        packet_buffer_state = PACKET_STATE_PAYLOAD;
                    else
                        packet_buffer_state = PACKET_STATE_CRC16;
                }
                else
                {
                    packet_buffer_index = 0;
                    packet_buffer_state = PACKET_STATE_HEADER;
                }
            }
            break;

        case PACKET_STATE_PAYLOAD:
            if (packet_buffer_index == PACKET_PAYLOAD_OFFSET + packet_payload_len)
            {
                log_d("payload ok");
                packet_buffer_state = PACKET_STATE_CRC16;
            }
            break;

        case PACKET_STATE_CRC16:
            if (packet_buffer_index == PACKET_PAYLOAD_OFFSET + packet_payload_len + PACKET_CRC_SIZE)
            {
                led_off(led3);
                uint16_t crc = get_u16(packet_buffer + PACKET_PAYLOAD_OFFSET + packet_payload_len);
                uint16_t ccrc = crc16(packet_buffer, packet_payload_len + PACKET_PAYLOAD_OFFSET);
                if (crc == ccrc)
                {
                    log_d("crc ok");
                    log_d("expect: %02X, recv: %02X", crc, ccrc);
                    log_d("receive packet, opcode: %02X len: %d", packet_buffer[PACKET_OPCODE_OFFSET], packet_payload_len);
                    if (ELOG_LVL_VERBOSE <= LOG_LVL)
                    {
                        elog_hexdump("PACKET", 16, packet_buffer, PACKET_MIN_SIZE + packet_payload_len);
                    }
                }
                else
                {
                    log_e("crc error");
                    log_e("expect: %02X, recv: %02X", crc, ccrc);
                }
                packet_buffer_index = 0;
                packet_buffer_state = PACKET_STATE_HEADER;
                led_on(led3);
                return true;
            }
            break;

        default:
            break;
        }
    }
    return false;
}

static void bl_tx_packet_response(uint8_t opecode, uint8_t errcode, uint16_t len, const uint8_t *data)
{
    uint8_t *response = packet_buffer, *prsp = packet_buffer;
    put_u8_inc(&prsp, PACKET_HEADER_RESPONSE);
    put_u8_inc(&prsp, opecode);
    put_u8_inc(&prsp, errcode);
    put_u16_inc(&prsp, len);
    put_bytes_inc(&prsp, data, len);
    uint16_t crc = crc16(response, prsp - response);
    put_u16_inc(&prsp, crc);

    bl_usart_write(packet_buffer, prsp - response);
}

static void bl_opcode_inquery_handler(void)
{
    log_i("inquery start...");
    if (packet_payload_len != 1)
        return;

    subcode_inquery_t subcode = (subcode_inquery_t)packet_buffer[PACKET_PAYLOAD_OFFSET];
    uint8_t mtu[2];

    switch (subcode)
    {
    case SUBCODE_INQUERY_VERSION:
        bl_tx_packet_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_OK, sizeof(BL_VERSION), (const uint8_t *)BL_VERSION);
        break;
    case SUBCODE_INQUERY_MTU:
        mtu[0] = BL_PAYLOAD_BUFFER_SIZE & 0xff;
        mtu[1] = (BL_PAYLOAD_BUFFER_SIZE >> 8) & 0xff;
        bl_tx_packet_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_OK, sizeof(mtu), (const uint8_t *)mtu);
        break;
    default:
        break;
    }
}

static void bl_opcode_reset_handler(void)
{
    log_i("reset handler");
    bl_tx_packet_response(PACKET_OPCODE_RESET, PACKET_ERRCODE_OK, 0, NULL);
    log_w("system reseting...");

    NVIC_SystemReset();
}

static void app_application(void)
{
    if (!magic_header_trap_app_check())
    {
        log_e("magic header trap app check failed, enter boot mode");
        return;
    }

    log_w("app application...");

    led_off(led1);
    LL_USART_DeInit(USART1);
    LL_USART_DeInit(USART3);
    LL_TIM_DeInit(TIM6);
    NVIC_DisableIRQ(USART1_IRQn);
    NVIC_DisableIRQ(USART3_IRQn);
    NVIC_DisableIRQ(TIM6_DAC_IRQn);

    SCB->VTOR = APP_ADDRESS;

    extern void JumptoApp(uint32_t appAddress);
    JumptoApp(APP_ADDRESS);
}

static void bl_opcode_boot_handler(void)
{
    log_i("boot handler");
    bl_tx_packet_response(PACKET_OPCODE_BOOT, PACKET_ERRCODE_OK, 0, NULL);
    log_w("system boot...");

    app_application();
}

static void bl_opcode_erase_handler(void)
{
    log_i("erase handler...");

    if (packet_payload_len != 8)
    {
        log_e("erase packet len error, len=%d", packet_payload_len);
        bl_tx_packet_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    uint32_t addr = get_u32(packet_buffer + PACKET_PAYLOAD_OFFSET);
    uint32_t size = get_u32(packet_buffer + PACKET_PAYLOAD_OFFSET + ADDR_PARAM_LENGTH);

    if (addr < STM32_FLASH_BASE_ADDR || addr + size > STM32_FLASH_BASE_ADDR + STM32_FLASH_SIZE)
    {
        log_e("erase address out of range, addr=0x%08X, size=0x%08X", addr, size);
        bl_tx_packet_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    if (addr < BL_FLASH_BASE_ADDR + BL_FLASH_SIZE && addr + size > BL_FLASH_BASE_ADDR)
    {
        log_e("erase address has been locked, addr=0x%08X, size=0x%08X", addr, size);
        bl_tx_packet_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    if (size == 0)
    {
        log_e("erase size is 0");
        bl_tx_packet_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    log_i("erase address: 0x%08X, size: 0x%08X", addr, size);

    stm32_flash_unlock();
    stm32_flash_erase(addr, size);
    stm32_flash_lock();
    bl_tx_packet_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_OK, 0, NULL);
}

static void bl_opcode_program_handler(void)
{
    log_i("program handler...");

    if (packet_payload_len <= 8)
    {
        log_e("program packet len error, len=%d", packet_payload_len);
        bl_tx_packet_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    uint32_t addr = get_u32(packet_buffer + PACKET_PAYLOAD_OFFSET);
    uint32_t size = get_u32(packet_buffer + PACKET_PAYLOAD_OFFSET + ADDR_PARAM_LENGTH);
    uint8_t *data = (uint8_t *)(packet_buffer + PACKET_PAYLOAD_OFFSET + ADDR_SIZE_PARAM_LENGTH);

    if (addr < STM32_FLASH_BASE_ADDR || addr + size > STM32_FLASH_BASE_ADDR + STM32_FLASH_SIZE)
    {
        log_e("program address out of range, addr=0x%08X, size=0x%08X", addr, size);
        bl_tx_packet_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    if (addr < BL_FLASH_BASE_ADDR + BL_FLASH_SIZE && addr + size > BL_FLASH_BASE_ADDR)
    {
        log_e("program address has been locked, addr=0x%08X, size=0x%08X", addr, size);
        bl_tx_packet_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    if (size == 0)
    {
        log_e("program size is 0");
        bl_tx_packet_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    log_i("program address: 0x%08X, size: 0x%08X", addr, size);

    led_on(led2);
    stm32_flash_unlock();
    stm32_flash_program(addr, data, size);
    stm32_flash_lock();
    led_off(led2);

    bl_tx_packet_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_OK, 0, NULL);
}

static void bl_opcode_verify_handler(void)
{
    log_i("verify handler...");

    if (packet_payload_len != 12)
    {
        log_e("verify packet len error, len=%d", packet_payload_len);
        bl_tx_packet_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    uint32_t addr = get_u32(packet_buffer + PACKET_PAYLOAD_OFFSET);
    uint32_t size = get_u32(packet_buffer + PACKET_PAYLOAD_OFFSET + ADDR_PARAM_LENGTH);
    uint32_t crc = get_u32(packet_buffer + PACKET_PAYLOAD_OFFSET + ADDR_SIZE_PARAM_LENGTH);

    if (addr < STM32_FLASH_BASE_ADDR || addr + size > STM32_FLASH_BASE_ADDR + STM32_FLASH_SIZE)
    {
        log_e("verify address out of range, addr=0x%08X, size=0x%08X", addr, size);
        bl_tx_packet_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    if (addr < BL_FLASH_BASE_ADDR + BL_FLASH_SIZE && addr + size > BL_FLASH_BASE_ADDR)
    {
        log_e("verify address has been locked, addr=0x%08X, size=0x%08X", addr, size);
        bl_tx_packet_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    if (size == 0)
    {
        log_e("verify size is 0");
        bl_tx_packet_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    log_i("verify address: 0x%08X, size: 0x%08X", addr, size);

    uint32_t ccrc = crc32((const unsigned char *)addr, size);
    if (ccrc != crc)
    {
        log_e("verify crc error, expect=0x%08X, recv=0x%08X", ccrc, crc);
        bl_tx_packet_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_VERIFY, 0, NULL);
        return;
    }

    log_i("verify crc ok, expect=0x%08X, recv=0x%08X", ccrc, crc);

    bl_tx_packet_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_OK, 0, NULL);
}

static void bl_opcode_dispatch(void)
{
    switch (packet_buffer[PACKET_OPCODE_OFFSET])
    {
    case PACKET_OPCODE_INQUERY:
        bl_opcode_inquery_handler();
        break;
    case PACKET_OPCODE_RESET:
        bl_opcode_reset_handler();
        break;
    case PACKET_OPCODE_BOOT:
        bl_opcode_boot_handler();
        break;
    case PACKET_OPCODE_ERASE:
        bl_opcode_erase_handler();
        break;
    case PACKET_OPCODE_PROGRAM:
        bl_opcode_program_handler();
        break;
    case PACKET_OPCODE_VERIFY:
        bl_opcode_verify_handler();
        break;
    default:
        break;
    }
}

static void bl_usart_rx_handler(uint8_t *data, uint16_t size)
{
    rb_writes(rb, data, size);
}

static bool key_trap_boot_check(void)
{
    for (int i = 0; i < BL_TRAP_BOOT_TIME; i+=10)
    {
        time_delay_ms(10);
        if (!key_read(key2))
        {
            log_w("key not pressed 3s");
            return false;
        }
    }
    return true;
}

static bool rb_revc_trap_boot_check(void)
{
    for (int i = 0; i < BL_TRAP_BOOT_TIME; i+=10)
    {
        time_delay_ms(10);
        if (!rb_is_empty(rb))
        {
            log_w("rx data received, enter boot mode");
            return true;
        }
    }
    return false;
}

static void key_wait_release(void)
{
    while (key_read(key2))
        time_delay_ms(10);
}

static bool key_press_check(void)
{
    if (key_read(key2))
    {
        time_delay_ms(10);
        if (key_read(key2))
            return true;
    }
    return false;
}

void bootloader_main(void)
{
    log_i("Bootloader start...");

    bl_usart_init();
    bl_usart_rx_callback_register(bl_usart_rx_handler);
    rb = rb_init(rb_buffer, sizeof(rb_buffer));

    key_init(key2);

    bool trap_app = !key_trap_boot_check() && magic_header_trap_app_check() && !rb_revc_trap_boot_check();
    if (trap_app)
    {
        log_w("trap app ok, enter app mode");
        app_application();
    }
    else
    {
        log_w("trap app failed, enter boot mode");
    }

    led_init(led1);
    led_init(led2);
    led_init(led3);
    led_on(led1);

    key_wait_release();

    while (1)
    {
        if (key_press_check())
        {
            log_w("key2 pressed, system reseting...");
            time_delay_ms(2);
            NVIC_SystemReset();
        }

        uint8_t data;
        if (rb_read(rb, &data))
        {
            if (bl_rx_packet_parse(&data, 1))
                bl_opcode_dispatch();
        }
    }
}
