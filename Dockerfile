FROM alpine:3.18 AS build
RUN apk add --no-cache gcc musl-dev fuse-dev git
WORKDIR /src
RUN git clone --depth 1 https://github.com/lek5on/shell_by_aleksey.git
WORKDIR /src/shell_by_aleksey
RUN gcc -D_FILE_OFFSET_BITS=64 -D_FUSE_USE_VERSION=30 main.c -lfuse -o myshell

FROM alpine:3.18
RUN apk add --no-cache fuse-dev util-linux && \
    mkdir -p /mnt/vfs

COPY --from=build /src/shell_by_aleksey/myshell /usr/local/bin/myshell
RUN chmod +x /usr/local/bin/myshell

# Запускаем контейнер с привилегиями, необходимыми для lsblk
ENTRYPOINT ["myshell"]
