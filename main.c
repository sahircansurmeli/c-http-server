#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8050
#define IP_ADDR "127.0.0.1"
#define BACKLOG 10

char *get_filename_ext(const char *filename) {
    char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}

// NEED TO FREE
char *get_mime_type(const char *filename) {
    char cmd[512], *mime_type, *ext;
    FILE *pipe;

    mime_type = malloc(256 * sizeof(char));
    if (mime_type == NULL) {
        return NULL;
    }

    ext = get_filename_ext(filename);

    if (strcmp(ext, "html") == 0) {
        sprintf(mime_type, "text/html");
    }
    else if (strcmp(ext, "css") == 0) {
        sprintf(mime_type, "text/css");
    }
    else if (strcmp(ext, "js") == 0) {
        sprintf(mime_type, "text/javascript");
    }
    else {
        sprintf(cmd, "file %s --mime -b", filename);
        pipe = popen(cmd, "r");
        fscanf(pipe, "%s", mime_type);
        pclose(pipe);
    }

    return mime_type;
}

// NEED TO FREE
char *get_date() {
    char *datestr;
    time_t rawtime;
    struct tm utctime;

    datestr = malloc(64 * sizeof(char));

    time(&rawtime);
    gmtime_r(&rawtime, &utctime);
    strftime(datestr, 64, "%a, %d %b %Y %T GMT", &utctime);

    return datestr;
}

void send_response(int socket, int code, const char *msg, const void *body, const char *type, size_t len) {
    char header[BUFSIZ/2], *date, *response;
    int header_ptr, res_ptr;
    unsigned long res_len;

    if (body == NULL || len == 0) {
        body = NULL;
        len = 0;
    }

    date = get_date();
    header_ptr = sprintf(header, "HTTP/1.1 %d %s\r\nDate: %s\r\nConnection: close\r\nContent-Length: %zu\r\n", code, msg, date, len);

    if (len > 0) {
        header_ptr += sprintf(header + header_ptr, "Content-Type: %s;charset=utf-8\r\nX-Content-Type-Options: nosniff\r\n", type);
    }

    header_ptr += sprintf(header + header_ptr, "\r\n");
    res_len = header_ptr * sizeof(char) + len;
    response = malloc(res_len);
    res_ptr = sprintf(response, "%s", header);

    if (len > 0) {
        memcpy(response + res_ptr, body, len);
    }

    send(socket, response, res_len, 0);

    free(date);
    free(response);
}

void send_code(int socket, int code, const char *status) {
    send_response(socket, code, status, status, "text/plain", strlen(status));
}

void send_file(int socket, int fd, const char *mime_type) {
    struct stat statbuf;
    void *databuf;

    fstat(fd, &statbuf);
    databuf = malloc(statbuf.st_size);
    read(fd, databuf, statbuf.st_size);
    send_response(socket, 200, "OK", databuf, mime_type, statbuf.st_size);
}

void handle_request(int socket) {
    FILE *sockfp;
    char method[8], path[256], version[16], realpath[512], line[1024], path_last_char, *mime_type;
    int path_offset, fdin;

    sockfp = fdopen(dup(socket), "r");
    fscanf(sockfp, "%s%s%s", method, path, version);

    if (strcmp(method, "GET") != 0) {
        send_code(socket, 405, "Method Not Allowed");
        fclose(sockfp);
        return;
    }

    path_offset = sprintf(realpath, "public3%s", path);
    path_last_char = path[strlen(path) - 1];
    if (path_last_char == '/') {
        sprintf(realpath + path_offset, "index.html");
    }

    fdin = open(realpath, O_RDONLY);
    if (fdin < 0) {
        send_code(socket, 404, "Not Found");
        fclose(sockfp);
        return;
    }

    mime_type = get_mime_type(realpath);
    send_file(socket, fdin, mime_type);

    free(mime_type);
    close(fdin);
    fclose(sockfp);
}

int main() {
    int server_socket, client_socket;
    socklen_t server_addr_len;
    int b, l;
    struct sockaddr_in server_addr;

    signal(SIGCHLD, SIG_IGN);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(IP_ADDR);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Failed creating socket");
        return 1;
    }

    server_addr_len = sizeof(server_addr);
    b = bind(server_socket, (struct sockaddr *) &server_addr, server_addr_len);
    if (b < 0) {
        perror("Failed to bind");
        return 2;
    }

    l = listen(server_socket, BACKLOG);
    if (l < 0) {
        perror("Failed to listen");
        return 3;
    }

    printf("Listening...\n");

    while(1) {
        client_socket = accept(server_socket, (struct sockaddr *) NULL, NULL);

        if (!fork()) {
            printf("Fork child\n");
            handle_request(client_socket);
            close(client_socket);
            printf("Kill child\n");
            return EXIT_SUCCESS;
        }
    }

    return EXIT_SUCCESS;
}
