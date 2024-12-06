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

#define MAX_BUFFER 1024
#define HISTORY_FILE "command_log.txt"

#define RESET_STYLE "\033[0m"
#define ERROR_STYLE "\033[1;31m"
#define PROMPT_STYLE "\033[1;35m"

void reload_config(int signal_num);
void check_boot_sector(const char *device_name);
void init_virtual_fs();
void log_command(const char *cmd);
void show_history();
void close_shell();
void run_command(char *parameters[]);
void show_env_variable(char *param);
void handle_echo_command(char *parameters[]);
void configure_signals();
void execute_with_io_redirect(char *parameters[]);
void create_memory_snapshot(pid_t process_id);

void reload_config(int signal_num)
{
    const char *message = "\nConfiguration has been reloaded\n";
    write(STDERR_FILENO, message, strlen(message));
}

void configure_signals()
{
    struct sigaction action = {0};
    action.sa_handler = reload_config;
    sigaction(SIGHUP, &action, NULL);
    action.sa_handler = SIG_DFL;
    sigaction(SIGINT, &action, NULL);
}

void check_boot_sector(const char *device_name)
{
    unsigned char sector_data[512];
    char path_to_device[128];
    snprintf(path_to_device, sizeof(path_to_device), "/dev/%s", device_name);
    int fd = open(path_to_device, O_RDONLY);
    if (fd < 0)
    {
        perror("Failed to open device");
        return;
    }
    if (read(fd, sector_data, sizeof(sector_data)) != sizeof(sector_data))
    {
        perror("Failed to read sector");
        close(fd);
        return;
    }
    close(fd);
    if (sector_data[510] == 0x55 && sector_data[511] == 0xAA)
        printf("Disk %s is bootable\n", path_to_device);
    else
        printf("Disk %s is NOT bootable\n", path_to_device);
}

static int fs_get_attr(const char *path, struct stat *stat_buffer)
{
    memset(stat_buffer, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0)
    {
        stat_buffer->st_mode = S_IFDIR | 0755;
        stat_buffer->st_nlink = 2;
    }
    else if (strcmp(path, "/tasks") == 0)
    {
        stat_buffer->st_mode = S_IFREG | 0444;
        stat_buffer->st_nlink = 1;
        stat_buffer->st_size = 1024;
    }
    else
        return -ENOENT;
    return 0;
}

static int fs_read_dir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *info)
{
    if (strcmp(path, "/") != 0)
        return -ENOENT;
    filler(buffer, ".", NULL, 0);
    filler(buffer, "..", NULL, 0);
    filler(buffer, "tasks", NULL, 0);
    return 0;
}

static int fs_open_file(const char *path, struct fuse_file_info *info)
{
    if (strcmp(path, "/tasks") != 0)
        return -ENOENT;
    return 0;
}

static int fs_read_file(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *info)
{
    if (strcmp(path, "/tasks") != 0)
        return -ENOENT;
    FILE *cron_tasks = popen("crontab -l", "r");
    if (!cron_tasks)
        return -EIO;
    char task_list[1024];
    size_t read_len = fread(task_list, 1, sizeof(task_list), cron_tasks);
    pclose(cron_tasks);
    if (offset >= read_len)
        return 0;
    if (offset + size > read_len)
        size = read_len - offset;
    memcpy(buffer, task_list + offset, size);
    return size;
}

static struct fuse_operations fs_operations = {
    .getattr = fs_get_attr,
    .readdir = fs_read_dir,
    .open = fs_open_file,
    .read = fs_read_file,
};

void init_virtual_fs()
{
    if (mkdir("/tmp/custom_fs", 0755) == -1 && errno != EEXIST)
    {
        perror("Failed to create directory /tmp/custom_fs");
        return;
    }
    pid_t process_id = fork();
    if (process_id == 0)
    {
        if (setsid() < 0)
        {
            perror("Failed to create new session");
            exit(EXIT_FAILURE);
        }
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        open("/dev/null", O_RDONLY);
        open("/dev/null", O_WRONLY);
        open("/dev/null", O_WRONLY);
        char *arguments[] = {"fs_mount", "/tmp/custom_fs", "-f", "-o", "nonempty", NULL};
        if (fuse_main(5, arguments, &fs_operations, NULL) == -1)
        {
            perror("FUSE initialization error");
            exit(EXIT_FAILURE);
        }
        exit(0);
    }
    else if (process_id < 0)
    {
        perror("Fork error");
    }
    else
    {
        printf("Virtual filesystem mounted at /tmp/custom_fs\n");
    }
}

void log_command(const char *cmd)
{
    int file_desc = open(HISTORY_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (file_desc != -1)
    {
        write(file_desc, cmd, strlen(cmd));
        write(file_desc, "\n", 1);
        close(file_desc);
    }
}

void show_history()
{
    int file_desc = open(HISTORY_FILE, O_RDONLY);
    if (file_desc != -1)
    {
        char buffer[4096];
        ssize_t bytes;
        while ((bytes = read(file_desc, buffer, sizeof(buffer))) > 0)
            write(STDOUT_FILENO, buffer, bytes);
        close(file_desc);
    }
    else
        printf("No command history available.\n");
}

void close_shell()
{
    printf("\nExiting shell.\n");
    exit(0);
}

void run_command(char *parameters[])
{
    if (fork() == 0)
    {
        execvp(parameters[0], parameters);
        perror("Command execution failed");
        exit(EXIT_FAILURE);
    }
    else
    {
        int status;
        wait(&status);
    }
}

void show_env_variable(char *param)
{
    if (param[0] == '$')
    {
        char *value = getenv(param + 1);
        printf("%s\n", value ? value : "Variable not found.");
    }
}

void handle_echo_command(char *parameters[])
{
    for (int i = 1; parameters[i]; i++)
    {
        printf("%s%s", parameters[i], parameters[i + 1] ? " " : "\n");
    }
}

void execute_with_io_redirect(char *parameters[])
{
    int i = 0, append = 0;
    while (parameters[i])
    {
        if (!strcmp(parameters[i], ">"))
        {
            append = 0;
            break;
        }
        if (!strcmp(parameters[i], ">>"))
        {
            append = 1;
            break;
        }
        i++;
    }
    if (!parameters[i])
    {
        run_command(parameters);
        return;
    }
    if (!parameters[i + 1])
    {
        fprintf(stderr, ERROR_STYLE "Error: file is missing.\n" RESET_STYLE);
        return;
    }
    parameters[i] = NULL;
    char *filename = parameters[i + 1];
    if (fork() == 0)
    {
        int fd = open(filename, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
        if (fd < 0)
        {
            perror(ERROR_STYLE "File opening error" RESET_STYLE);
            exit(EXIT_FAILURE);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
        execvp(parameters[0], parameters);
        perror(ERROR_STYLE "Command execution failed" RESET_STYLE);
        exit(EXIT_FAILURE);
    }
    else
    {
        int status;
        wait(&status);
    }
}

void create_memory_snapshot(pid_t process_id)
{
    char maps_path[256], output_file[256];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", process_id);
    snprintf(output_file, sizeof(output_file), "snapshot_%d.txt", process_id);
    FILE *input = fopen(maps_path, "r"), *output = fopen(output_file, "w");
    if (!input)
    {
        perror("Error opening process maps");
        return;
    }
    if (!output)
    {
        perror("Snapshot creation failed");
        fclose(input);
        return;
    }
    char line[4096];
    while (fgets(line, sizeof(line), input))
        fputs(line, output);
    fclose(input);
    fclose(output);
    printf("Memory snapshot of process %d saved to %s\n", process_id, output_file);
}

int main()
{
    char input[MAX_BUFFER], *parameters[128], cwd[PATH_MAX];
    configure_signals();

    while (1)
    {
        if (getcwd(cwd, sizeof(cwd)))
            printf(PROMPT_STYLE "%s >> " RESET_STYLE, cwd);
        else
        {
            perror("Error retrieving current directory");
            printf(PROMPT_STYLE "> " RESET_STYLE);
        }
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin))
        {
            if (feof(stdin))
                close_shell();
            continue;
        }
        input[strcspn(input, "\n")] = 0;
        if (strlen(input))
            log_command(input);

        int i = 0;
        char *token = strtok(input, " ");
        while (token && i < 127)
            parameters[i++] = token, token = strtok(NULL, " ");
        parameters[i] = NULL;

        if (!parameters[0])
            continue;
        if (!strcmp(parameters[0], "exit"))
            close_shell();
        else if (!strcmp(parameters[0], "history"))
            show_history();
        else if (!strcmp(parameters[0], "setenv") && parameters[1])
            show_env_variable(parameters[1]);
        else if (!strcmp(parameters[0], "bootcheck") && parameters[1])
            check_boot_sector(parameters[1]);
        else if (!strcmp(parameters[0], "vfs"))
            init_virtual_fs();
        else if (!strcmp(parameters[0], "snapshot") && parameters[1])
            create_memory_snapshot(atoi(parameters[1]));
        else if (!strcmp(parameters[0], "echo"))
            handle_echo_command(parameters);
        else
            execute_with_io_redirect(parameters);
    }
    return 0;
}