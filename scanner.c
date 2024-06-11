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

#define SPECIAL_ADDRESS             0xFD
#define SPECIAL_CMD                 0x46
#define SPECIAL_CMD_LEGACY          0x60

#define CMD_EXT_SCAN_START          0x01
#define CMD_EXT_SCAN_NEXT           0x02
#define CMD_EXT_SCAN_RESP           0x03
#define CMD_EXT_SCAN_END            0x04
#define CMD_EXT_EVENTS_REQ          0x10
#define CMD_EXT_EVENTS_RESP         0x11
#define CMD_EXT_EVENTS_END          0x12
#define CMD_EXT_EVENTS_CTRL         0x18


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

static inline uint16_t u16_from_be_buf8(const uint8_t * buf)
{
    return (buf[0] << 8) + (buf[1] << 0);
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

/*
    Описание возможных команд которые отвечают устройства

    cmd:                    команда расширенная - то что лежит в buf[2]
    payload_len_index:      номер байта в буфере от которого зависит длина, если 0 то длина постоянна
    frame_len:              ожидаемая длина команды включая CRC

    Пример:

        ответ события отсутствуют
            FD 46 12 52 5D

            cmd = 0x12
            payload_len_index = PAYLOAD_LEN_FIXED (0)
            frame_len = 5

        ответ содержащий события

                           ,-- поле переменной длины 3 байта
                        ___|____
            0A 46 18 03 05 05 00 XX XX
                      |          -----
                      |           CRC
                      `-- длина (3)

            cmd = 0x18
            payload_len_index = 3
            frame_len = 6 (включая CRC без учета данных переменной длины

*/

static const cmd_len_desc_t ext_cmd_desc[] = {
    { .cmd = CMD_EXT_SCAN_RESP, .payload_len_index = PAYLOAD_LEN_FIXED, .frame_len = 10 },      // Функция ответа на сканирование
    { .cmd = CMD_EXT_SCAN_END, .payload_len_index = PAYLOAD_LEN_FIXED, .frame_len = 5 },        // Функция конца сканирования
    { .cmd = CMD_EXT_EVENTS_RESP, .payload_len_index = 5, .frame_len = 8 },                     // Функция ответа на запрос событий
    { .cmd = CMD_EXT_EVENTS_END, .payload_len_index = PAYLOAD_LEN_FIXED, .frame_len = 5 },      // Функция отсутствия событий
    { .cmd = CMD_EXT_EVENTS_CTRL, .payload_len_index = 3, .frame_len = 6 },                     // Функция ответа на конфигурирование событий
};

// стандартные функции для отдельной проверки внутри команды 0x09 (обрабатываются специально)
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
    // вся утилита работает только с расширенными ответами
    if ((buf[1] != SPECIAL_CMD) && (buf[1] != SPECIAL_CMD_LEGACY)) {
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

        // команда длиннее на 6 байт. так как PDU начинается с 7го байта, но не имеет байта с адресом устройства как стандартный пакет
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
            // print_hb("   <! ", rb, rdlen);
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

void send_special_cmd(uint8_t ext_cmd, uint8_t cmd, uint16_t len)
{
    tx_buf[0] = SPECIAL_ADDRESS;
    tx_buf[1] = ext_cmd;
    tx_buf[2] = cmd;
    send_cmd_in_tx_buf(len);
}

void send_special_read(uint8_t ext_cmd, uint32_t serial, uint16_t address, uint16_t len)
{
    u32_to_be_buf8(&tx_buf[3], serial);
    tx_buf[7] = 3;          // read multiple holding registers
    u16_to_be_buf8(&tx_buf[8], address);
    u16_to_be_buf8(&tx_buf[10], len);
    send_special_cmd(ext_cmd, 8, 12);
}

void send_change_id_cmd(uint8_t ext_cmd, uint32_t serial, uint8_t new_id)
{
    uint16_t id16 = new_id;
    u32_to_be_buf8(&tx_buf[3], serial);
    tx_buf[7] = 6;          // write single holding register

    // 128 адрес регистра с slave адресом устройства
    u16_to_be_buf8(&tx_buf[8], HOLDREG_WB_SLAVE_ID);
    u16_to_be_buf8(&tx_buf[10], id16);
    send_special_cmd(ext_cmd, 8, 12);
}

void send_cmd_scan_init(uint8_t ext_cmd)
{
    if (debug) {
        printf("    send SCAN INIT");
    }
    send_special_cmd(ext_cmd, CMD_EXT_SCAN_START, 3);
}

void send_cmd_scan_next(uint8_t ext_cmd)
{
    if (debug) {
        printf("    send SCAN NEXT");
    }
    send_special_cmd(ext_cmd, CMD_EXT_SCAN_NEXT, 3);
}

int parse_special_responce_str(uint8_t * frame, char * str, int len)
{
    if (frame[0] != SPECIAL_ADDRESS) {
        printf("error: received frame have not special address\n");
        return -1;
    }

    // вызов check_cmd_in_rx_buffer при приеме уже проверил что команда или 0x60 или 0x46

    if (frame[2] != CMD_EXT_STD_PDU_RESP) {
        printf("error: received frame have not pdu sub cmd\n");
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

int check_parity_set_setting(char parity, tcflag_t * c_cflag)
{
    if ((parity == 'n') || (parity == 'N')) {
        *c_cflag &= ~PARENB;
        return 1;
    }
    if ((parity == 'e') || (parity == 'E')) {
        *c_cflag |= PARENB;
        *c_cflag &= ~PARODD;
        return 1;
    }
    if ((parity == 'o') || (parity == 'O')) {
        *c_cflag |= PARENB;
        *c_cflag |= PARODD;
        return 1;
    }
    return 0;
}

int configure_tty(int baud, char parity)
{
    speed_t baud_setting = B1152000;

    if (check_baud_get_setting(baud, &baud_setting)) {
        printf("Using baud %d\n", baud);
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


    if (check_parity_set_setting(parity, &tty.c_cflag)) {
        if (debug) {
            printf("Using parity %c\n", parity);
        }
    } else {
        printf("Parity %c is not supported!\n", parity);
        return -1;
    };

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

void tool_scan(uint8_t ext_cmd)
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
            send_cmd_scan_init(ext_cmd);
            scan_init = 0;
        } else {
            delay_frame();
            send_cmd_scan_next(ext_cmd);
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
            send_special_read(ext_cmd, dev_info.serial, 200, 20);
            len = read_responce(&r);
            if (len) {
                parse_special_responce_str(r, dev_info.model, 20);
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

void tool_change_id(uint8_t ext_cmd, uint32_t sn, int new_id)
{
    if ((new_id == 0) || (new_id > 247)) {
        printf("\r\n %d bad ID", new_id);
    } else {
        printf("Change ID for device with serial %12lld [%08X] New ID: %d\n", (uint64_t)sn, sn, new_id);
        send_change_id_cmd(ext_cmd, sn, new_id);
        uint8_t * ptr;
        read_responce(&ptr);
    }
}

void tool_event(uint8_t min_slave, uint8_t max_event_len, uint8_t confirm_slave_id, uint8_t flag)
{
    typedef struct {
        uint8_t ext_brodcast_id;
        uint8_t ext_cmd;
        uint8_t ext_sub_cmd;
        uint8_t arbitration_min_slave_id;
        uint8_t event_limit;
        uint8_t confirm_slave_id;
        uint8_t confirm_flag;
        uint8_t crc[2];
    } ext_modbus_event_resp_cmd_t;

    typedef struct {
        uint8_t len;
        uint8_t type;
        uint8_t event_id[2];
        uint8_t data[];
    } event_in_buffer_t;

    struct ext_modbus_event_resp {
        uint8_t slave_id;
        uint8_t ext_cmd;
        uint8_t sub_cmd;
        uint8_t flag;
        uint8_t events_num;
        uint8_t data_len;
        uint8_t data[];
    };

    delay_frame();

    if (debug) {
        printf("    send EVENT GET");
    }

    ext_modbus_event_resp_cmd_t * req_frame = (ext_modbus_event_resp_cmd_t *)tx_buf;
    req_frame->arbitration_min_slave_id = min_slave;
    req_frame->event_limit = max_event_len;
    req_frame->confirm_slave_id = confirm_slave_id;
    req_frame->confirm_flag = flag;
    send_special_cmd(SPECIAL_CMD , CMD_EXT_EVENTS_REQ, sizeof(ext_modbus_event_resp_cmd_t) - 2);

    struct ext_modbus_event_resp * resp;
    fflush(stdout);
    int len = read_responce((uint8_t **)&resp);

    if (resp->sub_cmd == CMD_EXT_EVENTS_END) {
        if (debug) {
            printf("NO EVENTS\n");
        }
        return;
    } else if (resp->sub_cmd == CMD_EXT_EVENTS_RESP) {
        if (debug) {
            printf("    device: %3d - events: %3d   flag: %1d   event data len: %03d   frame len: %03d\n", resp->slave_id, resp->events_num, resp->flag, resp->data_len, len);
        }
        unsigned event_len = resp->data_len;
        if (resp->data_len) {
            unsigned index = 0;
            while (index < event_len) {
                event_in_buffer_t * e = (event_in_buffer_t *)&resp->data[index];
                uint16_t event_id = u16_from_be_buf8(e->event_id);
                uint64_t val = 0;
                memcpy(&val, e->data, e->len);

                printf("Event type: %3d   id: %5d [%04X]   payload: %10lld   device %d\n",
                    e->type, event_id, event_id, val, resp->slave_id);

                index += sizeof(event_in_buffer_t) + e->len;
            }
        }

    } else {
        printf("event wrong cmd %02X\n", resp->sub_cmd);
    }

    return;
}

void tool_event_ctrl(int id, uint8_t type, uint16_t addr, uint8_t val)
{
    typedef struct __attribute__((__packed__)) {
        uint8_t type;
        uint8_t event_id[2];
        uint8_t len;
        uint8_t ctrl;
    } event_ctrl_t;

    tx_buf[0] = id;
    tx_buf[1] = SPECIAL_CMD;
    tx_buf[2] = CMD_EXT_EVENTS_CTRL;
    tx_buf[3] = sizeof(event_ctrl_t);      // fixed only one reg config

    event_ctrl_t * ectrl = (event_ctrl_t *)&tx_buf[4];

    ectrl->type = type;
    u16_to_be_buf8(ectrl->event_id, addr);
    ectrl->len = 1;
    ectrl->ctrl = val;

    send_cmd_in_tx_buf(9);

    uint8_t * r;
    int len = read_responce(&r);
    return;
}

void print_help(const char* argv0)
{
        printf(
            "Wirenboard modbus extension tool. version: " VERSION "\n"
            "Usage: %s -d device [-b baud] [-s sn] [-i id] [-D]\n"
            "\n"
            "Options:\n"
            "    -d device      TTY serial device\n"
            "    -b baud        Baudrate, default 9600\n"
            "    -p parity      Parity, can be n|e|o, default n\n"
            "    -L             use 0x60 (deprecated) cmd instead of 0x46 in scan\n"
            "    -s sn          device sn\n"
            "    -i id          slave id\n"
            "    -D             debug mode\n"
            "    -l len         max len of event data field\n"
            "    -e id          event request with confirm 0 for slave id\n"
            "    -E id          event request with confirm 1 for slave id\n"
            "    -r reg         event control reg\n"
            "    -t type        event control type\n"
            "    -c ctrl        event control value\n"
            "    -h             show help\n"
            "\n"
            "For scan use:              %s -d device [-b baud] [-D]\n"
            "For scan some old fw use:  %s -d device [-b baud] -L [-D]\n"
            "For set slave id use:      %s -d device [-b baud] -s sn -i id [-D]\n"
            "For setup event use:       %s -d device [-b baud] -i id -r reg -t type -c ctrl\n"
            "Event request examples:\n"
            "         %s -d device [-b baud] -e 0               (request + nothing to confirm)\n"
            "         %s -d device [-b baud] -e 4               (request + confirm events from slave 4 flag 0)\n"
            "         %s -d device [-b baud] -E 6               (request + confirm events from slave 6 flag 1)\n"
            , argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0);
}

int main(int argc, char *argv[])
{
    if (argc == 1) {
        print_help(argv[0]);
        return EXIT_INVALIDARGUMENT;
    }

    int c;
    int baud = 9600;
    char parity = 'n';
    uint64_t sn = 0;
    int id = 0;
    uint8_t ext_cmd = SPECIAL_CMD;

    // events options
    int confirm_id = 0;     // events confirm slave id
    int event_request = 0;  // events request cmd + confirm flag value
    int maxlen = 0xFF;      // max len of events field in responce
    int ev_r = -1;          // event register address
    int ev_t = -1;          // event register type
    int ev_c = -1;          // event ctrl value

    while ((c = getopt(argc, argv, "d:b:Ls:i:l:r:t:c:e:p:E:Dh")) != -1) {
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

        case 'p':
            sscanf(optarg, "%c", &parity);
            break;

        case 'L':
            ext_cmd = SPECIAL_CMD_LEGACY;
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

// events options

        case 'l':
            sscanf(optarg, "%d", &maxlen);
            break;

        case 'r':
            sscanf(optarg, "%d", &ev_r);
            break;

        case 't':
            sscanf(optarg, "%d", &ev_t);
            break;

        case 'c':
            sscanf(optarg, "%d", &ev_c);
            break;

        case 'e':
            event_request = 1;
            sscanf(optarg, "%d", &confirm_id);
            break;

        case 'E':
            event_request = 2;
            sscanf(optarg, "%d", &confirm_id);
            break;

        default:
            print_help(argv[0]);
            return EXIT_INVALIDARGUMENT;
        }
    }

    if (fd == 0) {
        printf("Serial port not specified\n");
        return EXIT_INVALIDARGUMENT;
    }

    if (configure_tty(baud, parity) != 0) {
        return EXIT_FAILURE;
    }

    if (event_request) {
        if (maxlen > 0xFF) {
            maxlen == 0xFF;
        }
        tool_event(id, maxlen, confirm_id,  event_request - 1);
        return 0;
    }
    if (ev_r != -1) {
        if ((ev_r < 0) || (ev_r > 0xFFFF)) {
            printf("WRONG reg\n");
            return 1;
        }
        if (ev_t != 15 && ((ev_t < 1) || (ev_t > 4))) {
            // support types for standard regtypes and 15 as system type
            printf("WRONG type\n");
            return 1;
        }
        if ((ev_c < 0) || (ev_c > 2)) {
            printf("WRONG control\n");
            return 1;
        }
        if ((id < 1) || (id > 247)) {
            printf("WRONG id\n");
            return 1;
        }
        tool_event_ctrl(id, ev_t, ev_r, ev_c);
        return 0;
    }

    if ((sn != 0) || (id != 0)) {
        if ((sn != 0) && (id != 0)) {
            tool_change_id(ext_cmd, sn, id);
        } else {
            printf("both sn and new id are necessary to change id\n");
            return EXIT_FAILURE;
        }
    } else {
        // scan function
        tool_scan(ext_cmd);
    }

    return EXIT_SUCCESS;
}
