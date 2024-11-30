#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

void check_boot_signature(const char *device) {
    unsigned char buffer[512];
    char device_path[128];
    snprintf(device_path, sizeof(device_path), "/dev/%s", device);
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) { 
        perror("Ошибка открытия устройства"); 
        return; 
    }
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
    if (bytes_read < 0) {
        perror("Ошибка чтения устройства");
        close(fd);
        return;
    }

    // Проверка сигнатуры загрузочного сектора (0xAA55)
    if (buffer[510] == 0x55 && buffer[511] == 0xAA) {
        printf("Устройство %s является загрузочным\n", device);
    } else {
        printf("Устройство %s не является загрузочным\n", device);
    }
    close(fd);
}

void mount_vfs_cron() {
    if (mkdir("/tmp/vfs", 0755) == -1 && errno != EEXIST) {
        perror("Ошибка создания /tmp/vfs");
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = {"fuse_mount", "/tmp/vfs", NULL};
        execvp("fuse_mount", argv);
        perror("Ошибка FUSE");
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("Ошибка fork");
    } else {
        printf("VFS смонтирован в /tmp/vfs. Список задач cron доступен.\n");
    }
}

void dump_memory(pid_t pid) {
    char filename[64];
    snprintf(filename, sizeof(filename), "memory_dump_%d.txt", pid);
    FILE *file = fopen(filename, "w");
    if (!file) {
        perror("Ошибка создания файла дампа памяти");
        return;
    }

    char proc_file[64];
    snprintf(proc_file, sizeof(proc_file), "/proc/%d/mem", pid);
    FILE *proc = fopen(proc_file, "r");
    if (!proc) {
        perror("Ошибка открытия файла памяти процесса");
        fclose(file);
        return;
    }

    char buffer[256];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), proc)) > 0) {
        fwrite(buffer, 1, bytes_read, file);
    }

    fclose(proc);
    fclose(file);
    printf("Дамп памяти процесса %d сохранён в файл %s\n", pid, filename);
}

void reload_configuration() {
    printf("Configuration Reloaded\n");
}

int main() {
    char command[256];
    printf("Welcome to shell_by_aleksey\n");
    
    while (1) {
        printf("shell_by_aleksey> ");
        fgets(command, sizeof(command), stdin);

        // Убираем символ новой строки в конце команды
        command[strcspn(command, "\n")] = 0;

        if (strcmp(command, "exit") == 0 || strcmp(command, "\q") == 0) {
            printf("Выход из shell_by_aleksey...\n");
            break;
        }

        if (strncmp(command, "\l ", 3) == 0) {
            // Проверка загрузочного сектора
            const char *device = command + 3;
            check_boot_signature(device);
        } else if (strcmp(command, "\cron") == 0) {
            // Монтирование VFS для задач cron
            mount_vfs_cron();
        } else if (strncmp(command, "\mem ", 5) == 0) {
            // Дамп памяти процесса
            pid_t pid = atoi(command + 5);
            dump_memory(pid);
        } else if (strcmp(command, "history") == 0) {
            // Просмотр истории команд
            system("history");
        } else if (strncmp(command, "\e $", 4) == 0) {
            // Просмотр переменных окружения
            const char *var = command + 4;
            char value[128];
            if (getenv(var)) {
                snprintf(value, sizeof(value), "%s=%s", var, getenv(var));
                printf("%s\n", value);
            } else {
                printf("Переменная окружения %s не найдена.\n", var);
            }
        } else if (strcmp(command, "reload") == 0) {
            // Перезагрузка конфигурации
            reload_configuration();
        } else {
            // Для всех остальных команд выполняем их как стандартные
            system(command);
        }
    }
    
    return 0;
}
