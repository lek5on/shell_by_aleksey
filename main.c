#define FUSE_USE_VERSION 30

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <dirent.h>
#include <fuse.h>
#include <limits.h>

#define MY_SHELL_MAX_INPUT 1024
#define HISTORY_FILE "history.txt"

// Прототипы функций
void handle_sighup(int sig);
void check_boot_signature(const char *device);
void mount_vfs_cron();
void save_history(const char *command);
void load_history();
void handle_exit();
void execute_command(char *args[]);
void print_env_variable(char *arg);
void builtin_echo(char *args[]);
void setup_signal_handlers();
void execute_command_with_redirection(char *args[]);
void dump_memory(pid_t pid);

// Обработчик сигнала SIGHUP
void handle_sighup(int sig) {
    write(STDERR_FILENO, "\nКонфигурация обновлена\n", 25);
}

// Установка обработчиков сигналов
void setup_signal_handlers() {
    struct sigaction sa = {0};
    sa.sa_handler = handle_sighup;
    sigaction(SIGHUP, &sa, NULL);
    sa.sa_handler = SIG_DFL; // Стандартное поведение для SIGINT
    sigaction(SIGINT, &sa, NULL);
}

// Проверка загрузочного сектора
void check_boot_signature(const char *device) {
    unsigned char buffer[512];
    char device_path[128];
    snprintf(device_path, sizeof(device_path), "/dev/%s", device);
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) { perror("Ошибка открытия устройства"); return; }
    if (read(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
        perror("Ошибка чтения сектора");
        close(fd);
        return;
    }
    close(fd);
    if (buffer[510] == 0x55 && buffer[511] == 0xAA)
        printf("Диск %s является загрузочным (сигнатура 0xAA55 найдена).\n", device_path);
    else
        printf("Диск %s не является загрузочным (сигнатура 0xAA55 отсутствует).\n", device_path);
}

// Операции VFS для cron
static int vfs_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (strcmp(path, "/tasks") == 0) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = 1024;
    } else return -ENOENT;
    return 0;
}

static int vfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    if (strcmp(path, "/") != 0) return -ENOENT;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, "tasks", NULL, 0);
    return 0;
}

static int vfs_open(const char *path, struct fuse_file_info *fi) {
    if (strcmp(path, "/tasks") != 0) return -ENOENT;
    return 0;
}

static int vfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    if (strcmp(path, "/tasks") != 0) return -ENOENT;
    FILE *cron = popen("crontab -l", "r");
    if (!cron) return -EIO;
    char tasks[1024];
    size_t len = fread(tasks, 1, sizeof(tasks), cron);
    pclose(cron);
    if (offset >= len) return 0;
    if (offset + size > len) size = len - offset;
    memcpy(buf, tasks + offset, size);
    return size;
}

static struct fuse_operations vfs_ops = {
    .getattr = vfs_getattr,
    .readdir = vfs_readdir,
    .open = vfs_open,
    .read = vfs_read,
};

// Монтирование VFS для cron
void mount_vfs_cron() {
    if (mkdir("/tmp/vfs", 0755) == -1 && errno != EEXIST) {
        perror("Ошибка создания /tmp/vfs");
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        if (setsid() < 0) {
            perror("Ошибка setsid");
            exit(EXIT_FAILURE);
        }
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        open("/dev/null", O_RDONLY);
        open("/dev/null", O_WRONLY);
        open("/dev/null", O_WRONLY);
        char *argv[] = {"vfs_cron", "/tmp/vfs", "-f", "-o", "nonempty", NULL};
        if (fuse_main(5, argv, &vfs_ops, NULL) == -1) {
            perror("Ошибка FUSE");
            exit(EXIT_FAILURE);
        }
        exit(0);
        } else if (pid < 0) {
        perror("Ошибка fork");
    } else {
        printf("VFS смонтирован в /tmp/vfs. Список задач cron доступен.\n");
    }
}

// Сохранение истории команд
void save_history(const char *command) {
    int fd = open(HISTORY_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd != -1) {
        write(fd, command, strlen(command));
        write(fd, "\n", 1);
        close(fd);
    }
}

// Загрузка истории команд
void load_history() {
    int fd = open(HISTORY_FILE, O_RDONLY);
    if (fd != -1) {
        char buffer[4096];
        ssize_t bytes;
        while ((bytes = read(fd, buffer, sizeof(buffer))) > 0) write(STDOUT_FILENO, buffer, bytes);
        close(fd);
    } else printf("История отсутствует.\n");
}

// Выход из шелла
void handle_exit() {
    printf("Выход из шелла.\n");
    exit(0);
}

// Выполнение команды
void execute_command(char *args[]) {
    if (fork() == 0) {
        execvp(args[0], args);
        perror("Ошибка выполнения команды");
        exit(EXIT_FAILURE);
    } else {
        int status;
        wait(&status);
    }
}

// Основной цикл шелла
int main() {
    char input[MY_SHELL_MAX_INPUT], *args[128], cwd[PATH_MAX];
    setup_signal_handlers();

    while (1) {
        if (getcwd(cwd, sizeof(cwd))) printf("%s >> ", cwd);
        else printf("> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            if (feof(stdin)) handle_exit();
            continue;
        }
        input[strcspn(input, "\n")] = 0;
        if (strlen(input)) save_history(input);

        int i = 0;
        char *token = strtok(input, " ");
        while (token && i < 127) {
            args[i++] = token;
            token = strtok(NULL, " ");
        }
        args[i] = NULL;

        if (!args[0]) continue;
        if (!strcmp(args[0], "exit") || !strcmp(args[0], "\\q")) handle_exit();
        else if (!strcmp(args[0], "history")) load_history();
        else if (!strcmp(args[0], "\\cron")) mount_vfs_cron();
        else execute_command(args);
    }
    return 0;
}