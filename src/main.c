//
// Created by Spencer Elliott on 2023-09-18.
//

#include <kos.h>
#include <dEngine.h>

int main(int argc, char** argv) {
    dEngine_Init(GL_11_RENDERER, 640, 480);

    while (1) {
        dEngine_HostFrame();
    }

    return 0;
}