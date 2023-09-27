//
// Created by Spencer Elliott on 2023-09-18.
//

#include <kos.h>
#include <GL/gl.h>
#include <GL/glkos.h>
#include <dEngine.h>
#include "renderer.h"
#include "world.h"
#include "profiler.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

extern uint8 romdisk[];
KOS_INIT_ROMDISK(romdisk);

#define DO_PROFILING 0

int main(int argc, char** argv) {
    glKosInit();

    setenv("CWD", "/cd", 1);
    //setenv("CWD", "/pc", 1);

    //renderer.statsEnabled = 1;

    dEngine_Init(GL_11_RENDERER, 640, 480);

    struct mallinfo malloc_info = mallinfo();
    printf("Memory stats after initializing:\n");
    printf("\tMax system = %10lu bytes\n", (unsigned long)malloc_info.usmblks);
    printf("\tSystem     = %10lu bytes\n", (unsigned long)(malloc_info.arena + malloc_info.hblkhd));
    printf("\tIn use     = %10lu bytes\n", (unsigned long)(malloc_info.uordblks + malloc_info.hblkhd));
    printf("\tAvailable  = %10lu bytes\n", (unsigned long)(malloc_info.arena + malloc_info.hblkhd) - (unsigned long)(malloc_info.uordblks + malloc_info.hblkhd));

#if DO_PROFILING
    uint32 count = 0;

    profiler_init("/pc/dEngine_prof.out");
    profiler_start();
    while (count < 30) {
#else
    while(1) {
#endif
        dEngine_HostFrame();

        pvr_stats_t stats;
        pvr_get_stats(&stats);

        //printf("FPS: %f\n", stats.frame_rate);
    #if DO_PROFILING
        count++;
    #endif
    }
    profiler_stop();

    return 0;
}