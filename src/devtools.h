#ifndef DEVTOOLS_H
#define DEVTOOLS_H

#include <kos.h>

#define DEVTOOLS_INTERFACE_VERSION 0x1

typedef enum devtools_id {
    DEVTOOLS_ID_HANDSHAKE = 0x0,
    DEVTOOLS_ID_ACK,
    DEVTOOLS_ID_PRINT,
    DEVTOOLS_ID_COUNT
} devtools_id_e;

typedef enum devtools_result {
    DEVTOOLS_RESULT_OK = 0x0,
    DEVTOOLS_RESULT_ERR,
    DEVTOOLS_RESULT_COUNT
} devtools_result_e;

typedef struct devtools_header {
    uint32_t command;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t length;
} devtools_header_t;

typedef void (*devtools_handler_t)(devtools_header_t *header);

#define DEVTOOLS_EMPTY_FUNCTION (devtools_handler_t)0

typedef struct devtools {
    int infile;
    int outfile;
    devtools_handler_t handlers[DEVTOOLS_ID_COUNT];
} devtools_t;

devtools_result_e devtools_init();
devtools_result_e devtools_cleanup();
devtools_result_e devtools_process_messages();
devtools_result_e devtools_read_buffer(size_t size, uint8_t *buffer);
devtools_result_e devtools_send_message(devtools_header_t *header, uint8_t *data);
void devtools_register_handler(devtools_id_e id, devtools_handler_t handler);
void devtools_unregister_handler(devtools_id_e id);

#endif