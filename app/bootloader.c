#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "stm32f4xx.h"
#include "stm32f4xx_ll_tim.h"
#include "stm32f4xx_ll_usart.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "bootloader.h"
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
#define LOG_LVL    ELOG_LVL_DEBUG
#include "elog.h"

#define QUEUE_PACKET_MAX_POOL 10

static QueueHandle_t empty_queue, parse_queue, program_queue;
static queue_packet_t *current_packet;

static uint32_t fireware_size, received_size, programed_size;
static uint32_t header_size, header_received_size, header_programed_size;
static subcode_request_t program_info = SUBCODE_REQUEST_FWINFO;

static void packet_reset(queue_packet_t *packet)
{
    memset(packet, 0, sizeof(*packet));
}

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

static bool bl_opcode_valid(uint8_t opcode)
{
    switch (opcode)
    {
    case PACKET_OPCODE_NONE:
    case PACKET_OPCODE_INQUERY:
    case PACKET_OPCODE_REQUEST:
    case PACKET_OPCODE_RESET:
    case PACKET_OPCODE_BOOT:
    case PACKET_OPCODE_ERASE:
    case PACKET_OPCODE_PROGRAM:
    case PACKET_OPCODE_VERIFY:
    case PACKET_OPCODE_READ:
        return true;
    default:
        return false;
    }
}

static void bl_usart_tx_response(uint8_t opcode, uint8_t errcode, uint16_t len, const uint8_t *data)
{
    uint8_t response[128], *prsp = response;
    put_u8_inc(&prsp, PACKET_HEADER_RESPONSE);
    put_u8_inc(&prsp, opcode);
    put_u8_inc(&prsp, errcode);
    put_u16_inc(&prsp, len);
    if (len > 0)
        put_bytes_inc(&prsp, data, len);
    uint16_t crc = crc16(response, prsp - response);
    put_u16_inc(&prsp, crc);

    bl_usart_write(response, prsp - response);
}

static void bl_opcode_inquery_handler(queue_packet_t* packet)
{
    log_i("inquery start...");
    if (packet->len != 1)
        return;

    subcode_inquery_t subcode = (subcode_inquery_t)packet->raw[PACKET_PAYLOAD_OFFSET];
    uint8_t mtu[2];

    if (subcode != SUBCODE_INQUERY_VERSION && subcode != SUBCODE_INQUERY_MTU)
    {
        log_e("inquery subcode error, subcode=%d", subcode);
        bl_usart_tx_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    switch (subcode)
    {
    case SUBCODE_INQUERY_VERSION:
        bl_usart_tx_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_OK, sizeof(BL_VERSION), (const uint8_t *)BL_VERSION);
        break;
    case SUBCODE_INQUERY_MTU:
        mtu[0] = BL_PAYLOAD_BUFFER_SIZE & 0xff;
        mtu[1] = (BL_PAYLOAD_BUFFER_SIZE >> 8) & 0xff;
        bl_usart_tx_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_OK, sizeof(mtu), (const uint8_t *)mtu);
        break;
    default:
        break;
    }
}

static void bl_opcode_request_handler(queue_packet_t* packet)
{
    log_i("request start...");

    if (packet->len != 1 + sizeof(uint32_t))
    {
        log_e("request packet len error, len=%d", packet->len);
        bl_usart_tx_response(PACKET_OPCODE_REQUEST, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    subcode_request_t subcode = (subcode_request_t)packet->raw[PACKET_PAYLOAD_OFFSET];

    if (subcode != SUBCODE_REQUEST_FWINFO && subcode != SUBCODE_REQUEST_MHINFO)
    {
        log_e("request subcode error, subcode=%d", subcode);
        bl_usart_tx_response(PACKET_OPCODE_REQUEST, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    uint8_t *payload = packet->payload + 1;
    switch (subcode)
    {
    case SUBCODE_REQUEST_FWINFO:
        fireware_size = get_u32(payload);
        log_i("request fireware size is set: %ld", fireware_size);
        received_size = 0;
        programed_size = 0;
        break;
    case SUBCODE_REQUEST_MHINFO:
        header_size = get_u32(payload);
        log_i("request magic header size is set: %ld", header_size);
        header_received_size = 0;
        header_programed_size = 0;
        break;
    default:
        break;
    }
    program_info = subcode;
    bl_usart_tx_response(PACKET_OPCODE_REQUEST, PACKET_ERRCODE_OK, 0, NULL);
}

static void bl_opcode_reset_handler(queue_packet_t* packet)
{
    log_i("reset handler");
    bl_usart_tx_response(PACKET_OPCODE_RESET, PACKET_ERRCODE_OK, 0, NULL);
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

    SysTick->CTRL = 0;
    SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    NVIC_ClearPendingIRQ(SysTick_IRQn);
    NVIC_ClearPendingIRQ(PendSV_IRQn);

    SCB->VTOR = APP_ADDRESS;
    __DSB();
    __ISB();
    extern void JumptoApp(uint32_t appAddress);
    JumptoApp(APP_ADDRESS);
}

static void bl_opcode_boot_handler(queue_packet_t* packet)
{
    log_i("boot handler");
    bl_usart_tx_response(PACKET_OPCODE_BOOT, PACKET_ERRCODE_OK, 0, NULL);
    log_w("system boot...");

    app_application();
}

static void bl_opcode_erase_handler(queue_packet_t* packet)
{
    log_i("erase handler...");

    if (packet->len != 8)
    {
        log_e("erase packet len error, len=%d", packet->len);
        bl_usart_tx_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    uint32_t addr = get_u32(packet->raw + PACKET_PAYLOAD_OFFSET);
    uint32_t size = get_u32(packet->raw + PACKET_PAYLOAD_OFFSET + ADDR_PARAM_LENGTH);

    if (addr < STM32_FLASH_BASE_ADDR || addr + size > STM32_FLASH_BASE_ADDR + STM32_FLASH_SIZE)
    {
        log_e("erase address out of range, addr=0x%08X, size=0x%08X", addr, size);
        bl_usart_tx_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    if (addr < BL_FLASH_BASE_ADDR + BL_FLASH_SIZE && addr + size > BL_FLASH_BASE_ADDR)
    {
        log_e("erase address has been locked, addr=0x%08X, size=0x%08X", addr, size);
        bl_usart_tx_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    if (size == 0)
    {
        log_e("erase size is 0");
        bl_usart_tx_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    log_i("erase address: 0x%08X, size: 0x%08X", addr, size);

    stm32_flash_unlock();
    stm32_flash_erase(addr, size);
    stm32_flash_lock();
    bl_usart_tx_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_OK, 0, NULL);
}

static bool bl_opcode_program_handler(queue_packet_t* packet)
{
    log_i("program handler...");

    if (packet->len <= ADDR_SIZE_PARAM_LENGTH)
    {
        log_e("program packet len error, len=%d", packet->len);
        bl_usart_tx_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, 0, NULL);
        return false;
    }

    uint32_t addr = get_u32(packet->raw + PACKET_PAYLOAD_OFFSET);
    uint32_t size = get_u32(packet->raw + PACKET_PAYLOAD_OFFSET + ADDR_PARAM_LENGTH);

    if (addr < STM32_FLASH_BASE_ADDR || addr >= STM32_FLASH_BASE_ADDR + STM32_FLASH_SIZE ||
        size > STM32_FLASH_BASE_ADDR + STM32_FLASH_SIZE - addr)
    {
        log_e("program address out of range, addr=0x%08X, size=0x%08X", addr, size);
        bl_usart_tx_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, 0, NULL);
        return false;
    }

    if (addr < BL_FLASH_BASE_ADDR + BL_FLASH_SIZE && addr + size > BL_FLASH_BASE_ADDR)
    {
        log_e("program address has been locked, addr=0x%08X, size=0x%08X", addr, size);
        bl_usart_tx_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, 0, NULL);
        return false;
    }

    if (size == 0 || size != packet->len - ADDR_SIZE_PARAM_LENGTH)
    {
        log_e("program size does not match payload, size=%lu, len=%u", size, packet->len);
        bl_usart_tx_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, 0, NULL);
        return false;
    }

    log_i("program address: 0x%08X, size: 0x%08X", addr, size);

    uint32_t *received = program_info == SUBCODE_REQUEST_MHINFO ? &header_received_size : &received_size;
    uint32_t expected_size = program_info == SUBCODE_REQUEST_MHINFO ? header_size : fireware_size;
    *received += size;
    bool more_data = *received < expected_size;

    BaseType_t result = xQueueSend(program_queue, &packet, portMAX_DELAY);
    configASSERT(result == pdPASS);

    if (more_data)
        bl_usart_tx_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_OK, 0, NULL);
    return true;
}

static void bl_opcode_verify_handler(queue_packet_t* packet)
{
    log_i("verify handler...");

    if (packet->len != 12)
    {
        log_e("verify packet len error, len=%d", packet->len);
        bl_usart_tx_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    uint32_t addr = get_u32(packet->raw + PACKET_PAYLOAD_OFFSET);
    uint32_t size = get_u32(packet->raw + PACKET_PAYLOAD_OFFSET + ADDR_PARAM_LENGTH);
    uint32_t crc = get_u32(packet->raw + PACKET_PAYLOAD_OFFSET + ADDR_SIZE_PARAM_LENGTH);

    if (addr < STM32_FLASH_BASE_ADDR || addr + size > STM32_FLASH_BASE_ADDR + STM32_FLASH_SIZE)
    {
        log_e("verify address out of range, addr=0x%08X, size=0x%08X", addr, size);
        bl_usart_tx_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    if (addr < BL_FLASH_BASE_ADDR + BL_FLASH_SIZE && addr + size > BL_FLASH_BASE_ADDR)
    {
        log_e("verify address has been locked, addr=0x%08X, size=0x%08X", addr, size);
        bl_usart_tx_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    if (size == 0)
    {
        log_e("verify size is 0");
        bl_usart_tx_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, 0, NULL);
        return;
    }

    log_i("verify address: 0x%08X, size: 0x%08X", addr, size);

    uint32_t ccrc = crc32((const unsigned char *)addr, size);
    if (ccrc != crc)
    {
        log_e("verify crc error, expect=0x%08X, recv=0x%08X", ccrc, crc);
        bl_usart_tx_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_VERIFY, 0, NULL);
        return;
    }

    log_i("verify crc ok, expect=0x%08X, recv=0x%08X", ccrc, crc);

    bl_usart_tx_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_OK, 0, NULL);
}

static void bl_usart_rx_handler(uint8_t *data, uint16_t size)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    while (size--)
    {
        uint8_t byte = *data++;

        queue_packet_t* packet = current_packet;

        switch (packet->_state)
        {
        case PACKET_STATE_HEADER:
            if (byte == PACKET_HEADER_REQUEST)
            {
                packet->header = PACKET_HEADER_REQUEST;
                packet->_recv = 0;
                packet->_state = PACKET_STATE_OPCODE;
            }
            else
            {
                packet->_recv = 0;
                packet->_state = PACKET_STATE_HEADER;
            }
            break;

        case PACKET_STATE_OPCODE:
            packet->opcode = byte;
            packet->_recv = 0;
            packet->_state = PACKET_STATE_LEN;
            break;

        case PACKET_STATE_LEN:
            packet->_temp[packet->_recv++] = byte;
            if (packet->_recv == PACKET_LENGTH_SIZE)
            {
                packet->_recv = 0;
                packet->len = get_u16(packet->_temp);
                if (packet->len > BL_PAYLOAD_BUFFER_SIZE)
                {
                    packet->_state = PACKET_STATE_HEADER;
                }
                else if (packet->len == 0)
                {
                    packet->_state = PACKET_STATE_CRC16;
                }
                else
                {
                    packet->_state = PACKET_STATE_PAYLOAD;
                    if (packet->opcode == PACKET_OPCODE_PROGRAM)
                    {
                        bl_usart_start_rx_aync(packet->payload, packet->len);
                    }
                }
            }
            break;

        case PACKET_STATE_PAYLOAD:
            packet->payload[packet->_recv++] = byte;
            if (packet->_recv == packet->len)
            {
                packet->_recv = 0;
                packet->_state = PACKET_STATE_CRC16;
            }
            break;

        case PACKET_STATE_CRC16:
            packet->_temp[packet->_recv++] = byte;
            if (packet->_recv == PACKET_CRC_SIZE)
            {
                led_on(led3);
                packet->_recv = 0;
                packet->_state = PACKET_STATE_HEADER;
                packet->crc = get_u16(packet->_temp);
                uint16_t ccrc = crc16(packet->raw, packet->len + PACKET_PAYLOAD_OFFSET);
                led_off(led3);
                if (packet->crc == ccrc)
                {
                    xQueueSendFromISR(parse_queue, &packet, &xHigherPriorityTaskWoken);
                    xQueueReceiveFromISR(empty_queue, &current_packet, &xHigherPriorityTaskWoken);
                    configASSERT(current_packet != NULL);
                    packet_reset(current_packet);
                }
                else
                {
                    packet->_recv = 0;
                    packet->_state = PACKET_STATE_HEADER;
                }
            }
            break;

        default:
            packet->_recv = 0;
            packet->_state = PACKET_STATE_HEADER;
            break;
        }
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void bl_usart_rx_async_handler(void)
{
    if (current_packet != NULL && current_packet->_state == PACKET_STATE_PAYLOAD)
        current_packet->_state = PACKET_STATE_CRC16;
}

static bool bl_opcode_dispatch(queue_packet_t* packet)
{
    switch (packet->opcode)
    {
    case PACKET_OPCODE_INQUERY:
        bl_opcode_inquery_handler(packet);
        break;
    case PACKET_OPCODE_REQUEST:
        bl_opcode_request_handler(packet);
        break;
    case PACKET_OPCODE_RESET:
        bl_opcode_reset_handler(packet);
        break;
    case PACKET_OPCODE_BOOT:
        bl_opcode_boot_handler(packet);
        break;
    case PACKET_OPCODE_ERASE:
        bl_opcode_erase_handler(packet);
        break;
    case PACKET_OPCODE_PROGRAM:
        return bl_opcode_program_handler(packet);
    case PACKET_OPCODE_VERIFY:
        bl_opcode_verify_handler(packet);
        break;
    default:
        break;
    }
    return false;
}

void parser_task(void *param)
{
    (void)param;
    while (1)
    {
        queue_packet_t* packet;
        xQueueReceive(parse_queue, &packet, portMAX_DELAY);
        configASSERT(packet != NULL);

        log_d("receive packet, opcode: %02X len: %d", packet->opcode, packet->len);

        if (!bl_opcode_valid(packet->opcode))
        {
            log_e("invalid opcode: %02X", packet->opcode);
            bl_usart_tx_response(packet->opcode, PACKET_ERRCODE_PARAM, 0, NULL);
            packet_reset(packet);
            xQueueSend(empty_queue, &packet, portMAX_DELAY);
            continue;
        }

        if (!bl_opcode_dispatch(packet))
            xQueueSend(empty_queue, &packet, portMAX_DELAY);
    }
}

static void run_program(queue_packet_t *packet)
{
    uint32_t addr = get_u32(packet->raw + PACKET_PAYLOAD_OFFSET);
    uint32_t size = get_u32(packet->raw + PACKET_PAYLOAD_OFFSET + ADDR_PARAM_LENGTH);
    uint8_t *data = (uint8_t *)(packet->raw + PACKET_PAYLOAD_OFFSET + ADDR_SIZE_PARAM_LENGTH);

    led_on(led2);
    stm32_flash_unlock();
    stm32_flash_program(addr, data, size);
    stm32_flash_lock();
    led_off(led2);

    uint32_t *programed = program_info == SUBCODE_REQUEST_MHINFO ? &header_programed_size : &programed_size;
    uint32_t expected_size = program_info == SUBCODE_REQUEST_MHINFO ? header_size : fireware_size;
    *programed += size;
    if (*programed == expected_size)
        bl_usart_tx_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_OK, 0, NULL);
}

void program_task(void *param)
{
    (void)param;
    while (1)
    {
        queue_packet_t *packet;
        xQueueReceive(program_queue, &packet, portMAX_DELAY);
        configASSERT(packet);

        run_program(packet);

        xQueueSend(empty_queue, &packet, portMAX_DELAY);
    }
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

static bool rx_trap_app(void)
{
    for (uint32_t i = 0; i < BL_TRAP_BOOT_TIME; i += 10)
    {
        if (bl_usart_rx_seen())
        {
            log_w("data received, trap into boot");
            return false;
        }        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return !bl_usart_rx_seen();
}

void bootloader_main(void)
{
    log_i("Bootloader start...");

    empty_queue = xQueueCreate(QUEUE_PACKET_MAX_POOL, sizeof(queue_packet_t*));
    configASSERT(empty_queue);
    parse_queue = xQueueCreate(QUEUE_PACKET_MAX_POOL, sizeof(queue_packet_t*));
    configASSERT(parse_queue);
    program_queue = xQueueCreate(QUEUE_PACKET_MAX_POOL, sizeof(queue_packet_t*));
    configASSERT(program_queue);

    for (uint8_t i = 0; i < QUEUE_PACKET_MAX_POOL; ++i)
    {
        queue_packet_t* packet = pvPortMalloc(sizeof(queue_packet_t));
        configASSERT(packet);
        memset(packet, 0, sizeof(*packet));
        BaseType_t result = xQueueSend(empty_queue, &packet, 0);
        configASSERT(result == pdPASS);
    }

    BaseType_t result = xQueueReceive(empty_queue, &current_packet, portMAX_DELAY);
    configASSERT(result == pdPASS);


    bl_usart_rx_callback_register(bl_usart_rx_handler);
    bl_usart_rx_async_callback_register(bl_usart_rx_async_handler);
    bl_usart_init();

    result = xTaskCreate(parser_task, "parser_task", 2048, NULL, 2, NULL);
    configASSERT(result == pdPASS);
    result = xTaskCreate(program_task, "program_task", 2048, NULL, 1, NULL);
    configASSERT(result == pdPASS);

    key_init(key2);

    bool trap_app = !key_trap_boot_check() && magic_header_trap_app_check() && rx_trap_app();
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
            vTaskDelay(pdMS_TO_TICKS(2));
            NVIC_SystemReset();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
