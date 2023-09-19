//
// Created by Spencer Elliott on 2023-09-18.
//

#include <kos.h>
#include <GL/gl.h>
#include <GL/glkos.h>
#include <dEngine.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

extern uint8 romdisk[];
KOS_INIT_ROMDISK(romdisk);

int main(int argc, char** argv) {
    glKosInit();

    setenv("CWD", "/cd", 1);

    dEngine_Init(GL_11_RENDERER, 640, 480);

    while (1) {
        dEngine_HostFrame();
    }

    return 0;
}