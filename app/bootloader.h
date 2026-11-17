#ifndef __BOOTLOADER_H__
#define __BOOTLOADER_H__


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

// OPCODE
typedef enum
{
    PACKET_OPCODE_NONE = 0,
    PACKET_OPCODE_RESET = 0x21,
    PACKET_OPCODE_BOOT = 0x22,
    PACKET_OPCODE_INQUERY = 0x01,
    PACKET_OPCODE_REQUEST = 0x02,
    PACKET_OPCODE_ERASE = 0x81,
    PACKET_OPCODE_PROGRAM = 0x82,
    PACKET_OPCODE_VERIFY = 0x83,
    PACKET_OPCODE_READ = 0x80,
} packet_opcode_t;

// INQUERY SUBCODE
// VERSION MTU
typedef enum
{
    SUBCODE_INQUERY_VERSION,
    SUBCODE_INQUERY_MTU,
} subcode_inquery_t;

// REQUEST SUBCODE
// FWINFO
typedef enum
{
    SUBCODE_REQUEST_FWINFO
} subcode_request_t;

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

typedef struct
{
    union
    {
        struct
        {
            uint8_t header;
            uint8_t opcode;
            uint16_t len;
            uint8_t payload[BL_PAYLOAD_BUFFER_SIZE];
            uint16_t crc;
        };
        uint8_t raw[4 + BL_PAYLOAD_BUFFER_SIZE + 2];
    };
    uint8_t _temp[4];
    uint16_t _recv;
    uint8_t _state;
} queue_packet_t;

#endif /* __BOOTLOADER_H__ */
