#include "kfork.h"

void exit(int status){
    kVagprintf("power off---------\n");
    asm volatile(
        "mov $1, %%eax\n"
        "int $0x80\n"
        :
        :
        : "eax"
    );
}
