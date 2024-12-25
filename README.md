
# Установите библиотеку FUSE
sudo apt install libfuse-dev

# Скачайте файл main.c и компилируйте его
gcc main.c -o myshell -D_FILE_OFFSET_BITS=64 -lfuse

# Запустите Shell с правами суперпользователя
sudo ./myshell

#Способ 2:
#Загрузите образ с Docker Hub:
docker pull lek5on/myshell:latest

#Запустите контейнер:
docker run --rm -it --privileged lek5on/myshell

изменение 1
изменение 2
