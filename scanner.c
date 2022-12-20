#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <getopt.h>
#include <time.h>
#include "modbus_crc.h"

#define EXIT_INVALIDARGUMENT        2

#define BUFFER_SIZE                 512
#define DEVICES_MAX                 100

#define READ_LEN                    16
#define RESPONCE_MIN_LEN            4

#define SCPECIAL_ADDRESS            0xFD
#define SCPECIAL_CMD                0x60

#define CMD_EXT_SCAN_START          0x01
#define CMD_EXT_SCAN_NEXT           0x02
#define CMD_EXT_SCAN_RESP           0x03
#define CMD_EXT_SCAN_END            0x04

#define CMD_EXT_STD_PDU_REQ         0x08
#define CMD_EXT_STD_PDU_RESP        0x09

#define HOLDREG_WB_SLAVE_ID         128


#define PAYLOAD_LEN_FIXED           0
#define PAYLOAD_EXT_OFFSET          7

int debug = 0;

int fd = 0;
struct timespec byte_send_time;
uint8_t rx_buf[BUFFER_SIZE];
uint8_t tx_buf[BUFFER_SIZE];

void delay_send(int len)
{
    for (int i = 0; i < len; i++) {
        nanosleep(&byte_send_time, NULL);
    }
}

void delay_frame(void)
{
    // 3,5 байта, для надежности 5
    delay_send(5);
}

static inline void u16_to_le_buf8(uint8_t * buf, uint16_t value)
{
    buf[0] = (value >> 0) & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
}

static inline void u16_to_be_buf8(uint8_t * buf, uint16_t value)
{
    buf[0] = (value >> 8) & 0xFF;
    buf[1] = (value >> 0) & 0xFF;
}

static inline uint16_t u16_from_le_buf8(const uint8_t * buf)
{
    return (buf[0] << 0) + (buf[1] << 8);
}

static inline void u32_to_be_buf8(uint8_t * buf, uint32_t value)
{
    buf[0] = (value >> 24) & 0xFF;
    buf[1] = (value >> 16) & 0xFF;
    buf[2] = (value >> 8) & 0xFF;
    buf[3] = (value >> 0) & 0xFF;
}

static inline uint32_t u32_from_be_buf8(const uint8_t * buf)
{
    return (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + (buf[3] << 0);
}

void print_hb(char * msg, uint8_t * b, int len)
{
    printf("%s : ", msg);
    for (int i = 0; i < len; i++) {
        printf(" %02X", b[i]);
    }
    printf("\r\n");
}

void send_cmd_in_tx_buf(uint8_t crc_offset)
{
    int len  = crc_offset + 2;
    u16_to_le_buf8(&tx_buf[crc_offset], modbus_crc(tx_buf, crc_offset));
    if (debug) {
        print_hb("    ->", tx_buf, len);
    }
    int wlen = write(fd, tx_buf, len);
    if (wlen != (crc_offset + 2)) {
        printf("Error from write: %d, %d\n", wlen, errno);
    }
    // ожидание фактического завершения асинхронной отправки
    delay_send(len);
}

typedef struct {
    uint8_t cmd;
    uint8_t payload_len_index;
    uint8_t frame_len;
} cmd_len_desc_t;

static const cmd_len_desc_t ext_cmd_desc[] = {
    { .cmd = CMD_EXT_SCAN_RESP, .payload_len_index = PAYLOAD_LEN_FIXED, .frame_len = 10 },      // Функция ответа на сканирование
    { .cmd = CMD_EXT_SCAN_END, .payload_len_index = PAYLOAD_LEN_FIXED, .frame_len = 5 },        // Функция конца сканирования
};

static const cmd_len_desc_t std_cmd_desc[] = {
    { .cmd = 0x01, .payload_len_index = 2, .frame_len = 5 },
    { .cmd = 0x02, .payload_len_index = 2, .frame_len = 5 },
    { .cmd = 0x03, .payload_len_index = 2, .frame_len = 5 },
    { .cmd = 0x04, .payload_len_index = 2, .frame_len = 5 },

    { .cmd = 0x05, .payload_len_index = PAYLOAD_LEN_FIXED, .frame_len = 8 },
    { .cmd = 0x06, .payload_len_index = PAYLOAD_LEN_FIXED, .frame_len = 8 },
    { .cmd = 0x0F, .payload_len_index = PAYLOAD_LEN_FIXED, .frame_len = 8 },
    { .cmd = 0x10, .payload_len_index = PAYLOAD_LEN_FIXED, .frame_len = 8 },
};

// находит структуру описывающую команду
const cmd_len_desc_t * get_cmd_len_desc(uint8_t cmd, int is_ext)
{
    const cmd_len_desc_t * desc;
    int desc_num;

    if (is_ext) {
        desc = ext_cmd_desc;
        desc_num = sizeof(ext_cmd_desc) / sizeof(ext_cmd_desc[0]);
    } else {
        desc = std_cmd_desc;
        desc_num = sizeof(std_cmd_desc) / sizeof(std_cmd_desc[0]);
    }

    for (int i = 0; i < desc_num; i++) {
        if (desc[i].cmd == cmd) {
            return &desc[i];
        }
    }
    return NULL;
}

int check_cmd_in_rx_buffer(uint8_t * buf, int available_len)
{
    if (buf[0] != SCPECIAL_ADDRESS) {
        return 0;
    }

    if (buf[1] != SCPECIAL_CMD) {
        return 0;
    }

    if (available_len < 2) {
        return 0;
    }

    uint8_t cmd = buf[2];
    int is_ext = 1;
    int additional_len = 0;

    int len = 0;

    if (cmd == CMD_EXT_STD_PDU_RESP) {
        // Функция ответа на стандартную команду обрабатывается отдельно
        if (available_len < PAYLOAD_EXT_OFFSET) {
            return 0;
        }

        // команда длинее на 6 байт. такак как PDU начинаектся с 7го байта но не имеет байта с адресом устройства как стандартный пакет
        additional_len = 6;
        is_ext = 0;
        cmd = buf[PAYLOAD_EXT_OFFSET];
    }

    const cmd_len_desc_t * desc = get_cmd_len_desc(cmd, is_ext);

    if (desc == NULL) {
        return 0;
    }

    if (desc->payload_len_index) {

        if (available_len < (additional_len + desc->payload_len_index)) {
            return 0;
        }

        len = buf[additional_len + desc->payload_len_index];
    }

    len += desc->frame_len;
    len += additional_len;

    return len;
}


int read_responce(uint8_t ** ptr)
{
    uint8_t * rb = rx_buf;

    while (1) {
        int rdlen = read(fd, rb, READ_LEN);
        if (rdlen > 0) {
            rb += rdlen;

            int data_len = rb - rx_buf;

            if (data_len > (BUFFER_SIZE - READ_LEN)) {
                printf("Error buffer overload\n");
                return 0;
            }

            if (data_len >= RESPONCE_MIN_LEN) {
                for (unsigned i = 0; i < data_len - RESPONCE_MIN_LEN + 1; i++) {
                    uint8_t * resp = &rx_buf[i];

                    int available_len = data_len - i;
                    int len = check_cmd_in_rx_buffer(resp, available_len);
                    if (len) {
                        if (len <= available_len) {
                            if (debug) {
                                print_hb("    <-", rx_buf, data_len);
                            }
                            if (modbus_crc(resp, len - 2) == u16_from_le_buf8(&resp[len - 2])) {
                                *ptr = resp;
                                return len;
                            } else {
                                printf("error: wrong crc\n");
                                return 0;
                            }
                        }
                    }
                }
            }

        } else if (rdlen < 0) {
            printf("Error from read: %d: %s\n", rdlen, strerror(errno));
        } else {
            // printf("Timeout from read\n");
        }
    }
    return 0;
}

void send_special_cmd(uint8_t cmd, uint16_t len)
{
    tx_buf[0] = SCPECIAL_ADDRESS;
    tx_buf[1] = SCPECIAL_CMD;
    tx_buf[2] = cmd;
    send_cmd_in_tx_buf(len);
}

void send_special_read(uint32_t serial, uint16_t address, uint16_t len)
{
    u32_to_be_buf8(&tx_buf[3], serial);
    tx_buf[7] = 3;
    u16_to_be_buf8(&tx_buf[8], address);
    u16_to_be_buf8(&tx_buf[10], len);
    send_special_cmd(8, 12);
}

void send_change_id_cmd(uint32_t serial, uint8_t new_id)
{
    uint16_t id16 = new_id;
    u32_to_be_buf8(&tx_buf[3], serial);
    tx_buf[7] = 6;

    // 128 адрес регистра с slave адресом устройства
    u16_to_be_buf8(&tx_buf[8], HOLDREG_WB_SLAVE_ID);
    u16_to_be_buf8(&tx_buf[10], id16);
    send_special_cmd(8, 12);
}

void send_cmd_scan_init(void)
{
    if (debug) {
        printf("    send SCAN INIT");
    }
    send_special_cmd(CMD_EXT_SCAN_START, 3);
}

void send_cmd_scan_next(void)
{
    if (debug) {
        printf("    send SCAN NEXT");
    }
    send_special_cmd(CMD_EXT_SCAN_NEXT, 3);
}

int parse_special_responnce_str(uint8_t * frame, char * str, int len)
{
    if (frame[0] != SCPECIAL_ADDRESS) {
        printf("error: recieved frame have not special address\n");
        return -1;
    }
    if (frame[1] != SCPECIAL_CMD) {
        printf("error: recieved frame have not special cmd\n");
        return -2;
    }
    if (frame[2] != CMD_EXT_STD_PDU_RESP) {
        printf("error: recieved frame have not pdu sub cmd\n");
        return -3;
    }

    for (size_t i = 0; i < len; i++) {
        str[i] = (char)frame[7 + 2 + 1 + (i * 2)];
    }
    return 0;
}

void print_termios(struct termios * tty)
{
    printf("  c_iflag   : %08X\r\n",    tty->c_iflag);
    printf("  c_oflag   : %08X\r\n",    tty->c_oflag);
    printf("  c_cflag   : %08X\r\n",    tty->c_cflag);
    printf("  c_lflag   : %08X\r\n",    tty->c_lflag);
    printf("  c_line    : %c\r\n",      tty->c_line);
    printf("  c_cc      : %s\r\n",      tty->c_cc);
    printf("  c_ispeed  : %08d\r\n",    tty->c_ispeed);
    printf("  c_ospeed  : %08d\r\n",    tty->c_ospeed);

}

int check_baud_get_setting(int param, speed_t * setup_val)
{
    static const int allowedBaudrates[] = { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600 };
    static const speed_t baud_settings[] = { B1200, B2400, B4800, B9600, B19200, B38400, B57600, B115200, B230400, B460800, B921600 };
    int valueIsIn = 0;
    for (unsigned int i = 0; i < (sizeof(allowedBaudrates) / sizeof(int)); i++){
        if (param == allowedBaudrates[i]) {
            *setup_val = baud_settings[i];
            valueIsIn = 1;
            break;
        }
    }
    return valueIsIn;
}

int configure_tty(int baud)
{
    speed_t baud_setting = B1152000;

    if (check_baud_get_setting(baud, &baud_setting)) {
        printf("Use baud %d\n", baud);
    } else {
        printf("Baudrate %d is not supported!\n", baud);
        return -1;
    };

    struct termios tty;

    if (tcgetattr(fd, &tty) < 0) {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        return -1;
    }

    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_cflag = 0x1CB2;
    tty.c_lflag = 0;

    cfsetospeed(&tty,(speed_t)baud_setting);
    cfsetispeed(&tty,(speed_t)baud_setting);

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    long nsec = 1000000000 / baud;

    // 12 бит в одном фрейме
    nsec *=  12;

    byte_send_time.tv_sec = 0;
    byte_send_time.tv_nsec = nsec;

    return 0;
}

void tool_scan(void)
{
    struct {
        uint32_t serial;
        uint8_t id
    } devices[DEVICES_MAX];

    typedef struct {
        uint32_t serial;
        uint8_t id;
        char model[20];
        uint32_t fwver;
    } dev_info_t;

    int dn = 0;

    int scan_init = 1;

    while (1) {
        if (scan_init) {
            send_cmd_scan_init();
            scan_init = 0;
        } else {
            delay_frame();
            send_cmd_scan_next();
        }

        uint8_t * r;
        int len = read_responce(&r);

        if (len == 0) {
            continue;
        }

        if (r[2] == CMD_EXT_SCAN_END) {
            printf("End SCAN\r\n");
            break;
        } else if (r[2] == CMD_EXT_SCAN_RESP) {
            if (len != 10) {
                printf("ERROR: scan responce len %d", len);
            }

            dev_info_t dev_info = {};

            dev_info.serial = u32_from_be_buf8(&r[3]);
            dev_info.id = r[PAYLOAD_EXT_OFFSET];

            int rpt = 0;
            for (int i = 0; i < dn; i++) {
                if (devices[i].id == dev_info.id) {
                    rpt = 1;
                }
            }
            devices[dn].id = dev_info.id;
            devices[dn].serial = dev_info.serial;

            delay_frame();

            if (debug) {
                printf("    read DEVICE MODEL\n");
            }
            send_special_read(dev_info.serial, 200, 20);
            len = read_responce(&r);
            if (len) {
                parse_special_responnce_str(r, dev_info.model, 20);
            }

            printf ("Found device (%2d) with serial %12lld [%08X]  modbus id: %3d  model: %-20s", dn + 1, (uint64_t)dev_info.serial, dev_info.serial, dev_info.id, dev_info.model);

            if (rpt) {
                printf("    [MODBUS ID REPEAT]");
            }

            dn++;

            printf("\r\n");
        } else {
            printf("ERROR: responce type %d", r[2]);
        }
    }
}

void tool_change_id(uint32_t sn, int new_id)
{
    if ((new_id == 0) || (new_id > 247)) {
        printf("\r\n %d bad ID", new_id);
    } else {
        printf("Chande ID for device with serial %12lld [%08X] New ID: %d\n", (uint64_t)sn, sn, new_id);
        send_change_id_cmd(sn, new_id);
        uint8_t * ptr;
        read_responce(&ptr);
    }
}

void print_help(char* argv0)
{
        printf(
            "Wiren Board extended Modbus scanner tool. version: " VERSION "\n"
            "Usage: %s -d device [-b baud] [-s sn] [-i id] [-D]\n"
            "\n"
            "Options:\n"
            "    -d device      TTY serial device  \n"
            "    -b baud        Baudrate, default 9600\n"
            "    -s sn          device sn\n"
            "    -i id          slave id\n"
            "    -D             debug mode\n"
            "\n"
            "For scan use:              %s -d device [-b baud] [-D]\n"
            "For set slave id use:      %s -d device [-b baud] -s sn -i id [-D]\n"
            , argv0, argv0, argv0);
}

int main(int argc, char *argv[])
{
    if (argc == 1) {
        print_help(argv[0]);
        return EXIT_INVALIDARGUMENT;
    }

    int c;
    int baud = 9600;
    uint64_t sn = 0;
    int id = 0;

    while ((c = getopt(argc, argv, "hd:b:s:i:D")) != -1) {
        switch(c) {
        case 'd':
            printf("Serial port: %s\n", optarg);
            fd = open(optarg, O_RDWR | O_NOCTTY | O_SYNC);
            if (fd < 0) {
                printf("Error opening port %s\n", strerror(errno));
                return EXIT_FAILURE;
            }
            break;

        case 'D':
            debug = 1;
            break;

        case 'b':
            sscanf(optarg, "%d", &baud);
            break;

        case 's':
            sscanf(optarg, "%lld", &sn);
            break;

        case 'i':
            sscanf(optarg, "%d", &id);
            break;

        case 'h':
            print_help(argv[0]);
            return EXIT_SUCCESS;

        default:
            print_help(argv[0]);
            return EXIT_INVALIDARGUMENT;
        }
    }

    if (fd == 0) {
        printf("Serial port not specified\n");
        return EXIT_INVALIDARGUMENT;
    }

    if (configure_tty(baud) != 0) {
        return EXIT_FAILURE;
    }

    if ((sn != 0) || (id != 0)) {
        if ((sn != 0) && (id != 0)) {
            tool_change_id(sn, id);
        } else {
            printf("both sn and new id necessery for change id\n");
            return EXIT_FAILURE;
        }
    } else {
        tool_scan();
    }

    return EXIT_SUCCESS;
}
