//
// Created by Spencer Elliott on 2023-09-18.
//

#ifdef DREAMCAST
#include <kos.h>
#include <GL/glkos.h>
#include <jimtcl/jim.h>
#endif

#include <dEngine.h>

#include "devtools.h"
#include "renderer.h"
#include "world.h"
#include "profiler.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

extern uint8 romdisk[];
KOS_INIT_ROMDISK(romdisk);

#define DO_PROFILING 0
#define PRINT_FPS 0
#define ENABLE_DEVTOOLS 1


int check_start(void) {
    maple_device_t *cont;
    cont_state_t *state;

    cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);

    if(!cont)
        return 0;

    /* Check for start on the controller */
    state = (cont_state_t *)maple_dev_status(cont);

    if(!state) {
        return 0;
    }

    if(state->buttons & CONT_START) {
        printf("Pressed start\n");
        return 1;
    }

    return 0;
}

void print_handler(devtools_header_t *hdr) {
    uint8_t str_buffer[hdr->length];

    if(devtools_read_buffer(hdr->length, str_buffer) != DEVTOOLS_RESULT_OK) {
        printf("Could not read text buffer from pipe file!\n");
        return;
    }

    printf("%s", (char *)str_buffer);
}

int main(int argc, char** argv) {
#if PRINT_FPS
    uint32_t counter = 0;
#endif

#if ENABLE_DEVTOOLS
    devtools_init();
    devtools_register_handler(DEVTOOLS_ID_PRINT, print_handler);
#endif


    Jim_Interp *jim;
    jim = Jim_CreateInterp();
    Jim_RegisterCoreCommands(jim);

    int retcode = Jim_EvalFile(jim, "/rd/hello.tcl");

    if (retcode != JIM_ERR) {
        int len;
        const char *msg = Jim_GetString(Jim_GetResult(jim), &len);
        printf("%s\n", msg);
    } else {
        printf("%s\n", Jim_String(Jim_GetResult(jim)));
    }

    glKosInit();
    printf("Using GLdc version: %s\n", GLDC_VERSION);

    //setenv("CWD", "/cd", 1);
    setenv("CWD", "/pc", 1);

    renderer.statsEnabled = 1;

    dEngine_Init(GL_11_RENDERER, 640, 480);

    struct mallinfo malloc_info = mallinfo();
    printf("Memory stats after initializing:\n");
    printf("\tTotal non-mmapped (arena) =               %10lu bytes\n", (unsigned long)malloc_info.arena);
    printf("\t# of free chunks (ordblks) =              %10lu\n", (unsigned long)malloc_info.ordblks);
    printf("\t# of free fastbin blocks (smblks) =       %10lu\n", (unsigned long)malloc_info.smblks);
    printf("\t# of mapped regions (hblks) =             %10lu\n", (unsigned long)malloc_info.hblks);
    printf("\tBytes in mapped regions (hblkhd) =        %10lu bytes\n", (unsigned long)malloc_info.hblkhd);
    printf("\tMax total allocated space (usmblks) =     %10lu bytes\n", (unsigned long)malloc_info.usmblks);
    printf("\tFree bytes held in fastbins (fsmblks) =   %10lu bytes\n", (unsigned long)malloc_info.fsmblks);
    printf("\tTotal allocated space (uordblks) =        %10lu bytes\n", (unsigned long)malloc_info.uordblks);
    printf("\tTotal free space (fordblks) =             %10lu bytes\n", (unsigned long)malloc_info.fordblks);
    printf("\tTopmost releasable block (keepcost) =     %10lu\n", (unsigned long)malloc_info.keepcost);

#if DO_PROFILING
    uint32 count = 0;

    profiler_init("/pc/dEngine_prof.out");
    profiler_start();
    while (count < 30) {
#else
    while(!check_start()) {
#if ENABLE_DEVTOOLS
        devtools_process_messages();
#endif

#endif
        dEngine_HostFrame();

#if PRINT_FPS
        pvr_stats_t stats;
        pvr_get_stats(&stats);

        counter += 1;

        if (counter % 30 == 0) {
            printf("FPS: %f\n", stats.frame_rate);
            counter = 0;
        }
#endif

    #if DO_PROFILING
        count++;
    #endif
    }
#if DO_PROFILING
    profiler_stop();
#endif

#if ENABLE_DEVTOOLS
    devtools_cleanup();
#endif

    return 0;
}