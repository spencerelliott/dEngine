#include "devtools.h"
#include <fcntl.h>
#include <sys/stat.h>

static devtools_t DEVTOOLS;
static uint32_t DEVTOOLS_INIT = 0;

devtools_result_e devtools_init() {
    if (DEVTOOLS_INIT) {
        return DEVTOOLS_RESULT_ERR;
    }

    printf("Opening infile for devtools\n");
    DEVTOOLS.infile = open("/pc/devtools.dc", O_RDONLY);

    printf("Setting infile to non-blocking\n");
    if(fcntl(DEVTOOLS.infile, F_SETFL, O_NONBLOCK) < 0) {
        printf("Could not setup infile to be non-blocking!\n");
        return DEVTOOLS_RESULT_ERR;
    }

    if (!DEVTOOLS.infile) {
        printf("Could not open devtools.dc file!\n");
        return DEVTOOLS_RESULT_ERR;
    }

    printf("Opening outfile for devtools\n");
    DEVTOOLS.outfile = open("/pc/devtools.pc", O_WRONLY);

    if (!DEVTOOLS.outfile) {
        printf("Could not open devtools.pc file!\n");
        return DEVTOOLS_RESULT_ERR;
    }

    DEVTOOLS_INIT = 1;

    return DEVTOOLS_RESULT_OK;
}

devtools_result_e devtools_cleanup() {
    if (!DEVTOOLS_INIT) {
        return DEVTOOLS_RESULT_ERR;
    }

    close(DEVTOOLS.infile);
    close(DEVTOOLS.outfile);

    DEVTOOLS_INIT = 0;

    return DEVTOOLS_RESULT_OK;
}

devtools_result_e devtools_process_messages() {
    devtools_header_t hdr;

    // Ignore this if dev tools haven't been initialized
    if (!DEVTOOLS_INIT) {
        return DEVTOOLS_RESULT_ERR;
    }

    // Process any commands that have been sent since the last frame. Note: commands are responsible
    // for emptying the data directly after the header so the next command can be processed correctly.
    while(read(DEVTOOLS.infile, &hdr, sizeof(devtools_header_t)) > 0) {
        if (hdr.command < DEVTOOLS_ID_COUNT && DEVTOOLS.handlers[hdr.command]) {
            DEVTOOLS.handlers[hdr.command](&hdr);
        }
    }

    return DEVTOOLS_RESULT_OK;
}

devtools_result_e devtools_read_buffer(size_t size, uint8_t *buffer) {
    // Ignore this if dev tools haven't been initialized
    if (!DEVTOOLS_INIT) {
        return DEVTOOLS_RESULT_ERR;
    }

    int read_count = read(DEVTOOLS.infile, buffer, size);

    if (!read_count) {
        return DEVTOOLS_RESULT_ERR;
    }

    return DEVTOOLS_RESULT_OK;
}

devtools_result_e devtools_send_message(devtools_header_t *header, uint8_t *data) {
    const size_t buffer_size = sizeof(devtools_header_t) + header->length;
    uint8_t buffer[buffer_size];

    // Fill the buffer that will be written to the pipe
    memcpy(buffer, header, sizeof(devtools_header_t));
    memcpy(&buffer[sizeof(devtools_header_t)], data, header->length);

    if (!DEVTOOLS_INIT) {
        return DEVTOOLS_RESULT_ERR;
    }

    int write_count = write(DEVTOOLS.outfile, buffer, buffer_size);

    if (!write_count) {
        return DEVTOOLS_RESULT_ERR;
    }

    return DEVTOOLS_RESULT_OK;
}

void devtools_register_handler(devtools_id_e id, const devtools_handler_t handler) {
    DEVTOOLS.handlers[id] = handler;
}

void devtools_unregister_handler(devtools_id_e id) {
    DEVTOOLS.handlers[id] = DEVTOOLS_EMPTY_FUNCTION;
}
