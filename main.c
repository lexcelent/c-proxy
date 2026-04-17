#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>

// strlen() function
#include <string.h>

// close() function
#include <unistd.h>

// atoi() function
#include <stdlib.h>

#define PORT 8081
#define BUFFER_SIZE 1024

void handle_connection(int client_fd);

int main() {
    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        fprintf(stderr, "error creating socket\n");
        return -1;
    }

    // server address settings
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT)
    };

    // bind socket to address
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        fprintf(stderr, "bind socket to address failed\n");
        close(server_fd);
        return -1;
    }


    if (listen(server_fd, 5) == -1) {
        fprintf(stderr, "listen failed\n");
        close(server_fd);
        return -1;
    }

    printf("listening on port %d\n", PORT);

    // now process client connections
    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
    
        if (client_fd == -1) {
            fprintf(stderr, "error accept client connection\n");
            continue;
        }

        // handle connection
        handle_connection(client_fd);
        // printf("client disconnected\n");
    }

    close(server_fd);

    return 0;
}

void handle_connection(int client_fd) {
    char buffer[BUFFER_SIZE] = {0};

    char cmdConnect[] = "CONNECT";

    int bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1);
    if (bytes_read > 0) {
        // last symbol
        buffer[bytes_read] = '\0';

        // if not HTTP CONNECT - close
        if (strncmp(buffer, cmdConnect, 7) != 0) {
            close(client_fd);
            return;
        }

        // parse "CONNECT www.host.ru:443 HTTP1/1\n\n" line
        char* first_space = strchr(buffer, ' ');
        char* colon = strchr(first_space + 1, ':');
        char* second_space = strchr(first_space + 1, ' ');
        
        char host[100] = {0};
        char port[6] = {0};

        int host_len = (int)(colon - first_space);
        strncpy(host, first_space + 1, host_len - 1);
        host[host_len] = '\0';

        int port_len = (int)(second_space - colon);
        strncpy(port, colon + 1, port_len - 1);
        port[port_len] = '\0';

        // printf("%s:%s - end\n", host, port);
        
        // create connect to host:port
        int target_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (target_fd == -1) {
            fprintf(stderr, "error creating socket\n");
            return;
        }

        int port_i = atoi(port);

        // target address settings
        struct sockaddr_in address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = htons(port_i)
        };

        // TODO: domain name resolve

        // finally, do CONNECT
        if (connect(target_fd, (struct sockaddr*)&address, sizeof(address)) != 0) {
            printf("connection with the target server failed\n");
            close(client_fd);
            return;
        }

        char buf[1024];
        int len = read(target_fd, buf, 1024);
        
        // TODO: proxying
        // incorrect...
        send(client_fd, buf, strlen(buf), 0);
        send(target_fd, buffer, strlen(buffer), 0);

        close(target_fd);
    }

    close(client_fd);
}

