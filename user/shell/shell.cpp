#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <format.h>
#include <syscall_def.h>
#include <file.h>

constexpr char* PATH[2] = {
    "/usr/bin/",
    "/"
};

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

int main(int argc, char** argv) {
    char buffer[32768];
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
            bool flag = false;
            char fn[256] = "/usr/bin/";
            for (int i = 0; i < 2; ++i) {
                int fd = open(strcat(fn, input), 1);
                if (fd == -1) continue;
                int size = read(fd, buffer, 32768);
                if (size == -1) continue;
                printf("Executing %s: %d bytes loaded\n", fn, size);
                char* v[] = {"rumianomnomnom\n", "yay\n", "can you hear me?"};
                exec(buffer, size, 1, 3, v);
                flag = true;
            }
            
            if (!flag) {
                print_rumia_text();
                printf(": Unknown command '%s'!\n", input);
            }

        }
        
        printf("\n");
    }
    return 0;
}