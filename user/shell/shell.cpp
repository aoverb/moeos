#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syscall_def.h>

void print_rumia_text() {
    set_color(0xF0B526);
    printf("Rumi");
    set_color(0xEB392D);
    printf("a");
    set_color(0xFFFFFF);
}

void print_lolios() {
    set_color(0xEB9D2F);
    printf("LoliOS");
    set_color(0xFFFFFF);
}

void main() {
    char input[256];
    printf("Shell is running in user addr: %x\n", &main);
    while (1) {
        print_lolios();
        
        printf(">");
        getline(input, 256);
        
        if (strcmp(input, "help") == 0) {
            printf("Hello user!");
            printf("This is ");
            print_lolios();
            printf("!\n");
            printf("The host here is ");
            print_rumia_text();
            printf("! Feel free!\n");
        } else if (strcmp(input, "") == 0) {
            continue;
        } else if (strcmp(input, "exit") == 0) {
            print_rumia_text();
            printf(": Goodbye, aoverb!\n");
            break;
        } else if (strcmp(input, "test") == 0) {
            asm volatile ("hlt");
        } else {
            print_rumia_text();
            printf(": Unknown command '%s'!\n", input);
        }
        
        printf("\n");
    }
}