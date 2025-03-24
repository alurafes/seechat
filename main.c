#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#define PANIC(message) {printf("Panic at %s:%d with message: %s\n", __FILE__, __LINE__, message); exit(EXIT_FAILURE);}
#define PANIC_ERRNO() {PANIC(strerror(errno));}
#define EXECUTE_OR_PANIC(action, fail_condition, message) {action; if (fail_condition) PANIC(message);}
#define EXECUTE_OR_PANIC_ERRNO(action, fail_condition) {EXECUTE_OR_PANIC(action, fail_condition, strerror(errno));}

#define SOCKET_LISTEN_BACKLOG (3)

#define BIND_ADDRESS ("127.0.0.1")
#define BIND_PORT (11701)

#define MAX_CLIENTS 10

typedef struct seechat_server_t 
{
    int fd;
} seechat_server_t;

typedef struct seechat_client_t 
{
    union 
    {
        struct sockaddr addr;
        struct sockaddr_in addr_in;
    } addr;
    int addr_len;
    int fd;
} seechat_client_t;

typedef enum seechat_result_t 
{
    SEECHAT_RESULT_SOCKET_FAIL,
    SEECHAT_RESULT_SOCKET_OPTIONS_FAIL,
    SEECHAT_RESULT_IP_CONVERSION_FAIL,
    SEECHAT_RESULT_BIND_FAIL,
    SEECHAT_RESULT_LISTEN_FAIL,
    SEECHAT_RESULT_ACCEPT_FAIL,
    SEECHAT_RESULT_SUCCESS,
    SEECHAT_RESULT_SIZE,
} seechat_result_t;

const char* seechat_result_get_message(seechat_result_t result)
{
    switch (result)
    {
        case SEECHAT_RESULT_SOCKET_FAIL: 
        case SEECHAT_RESULT_BIND_FAIL: 
        case SEECHAT_RESULT_LISTEN_FAIL: 
        case SEECHAT_RESULT_ACCEPT_FAIL: 
        case SEECHAT_RESULT_SOCKET_OPTIONS_FAIL: 
            return strerror(errno);
        case SEECHAT_RESULT_IP_CONVERSION_FAIL:
            return "Invalid IPv4 address";
        default:
            return "Missing error message";
    }
}

seechat_result_t seechat_create_server(const char* bind_address, uint16_t bind_port, seechat_server_t* server) 
{
    int result;
    
    result = socket(AF_INET, SOCK_STREAM, 0);
    if (result == -1) return SEECHAT_RESULT_SOCKET_FAIL;
    server->fd = result;

    result = setsockopt(server->fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    if (result == -1) return SEECHAT_RESULT_SOCKET_OPTIONS_FAIL;

    struct sockaddr_in addr_in = {0};
    addr_in.sin_port = htons(bind_port);
    addr_in.sin_family = AF_INET;

    result = inet_aton(bind_address, &addr_in.sin_addr);
    if (result == 0) return SEECHAT_RESULT_IP_CONVERSION_FAIL;

    result = bind(server->fd, (struct sockaddr*)&addr_in, sizeof(addr_in));
    if (result == -1) return SEECHAT_RESULT_BIND_FAIL;

    result = listen(server->fd, SOCKET_LISTEN_BACKLOG);
    if (result == -1) return SEECHAT_RESULT_LISTEN_FAIL;

    return SEECHAT_RESULT_SUCCESS;
}

int main(void)
{
    seechat_server_t server = {0};
    EXECUTE_OR_PANIC(seechat_result_t result = seechat_create_server(BIND_ADDRESS, BIND_PORT, &server), result != SEECHAT_RESULT_SUCCESS, seechat_result_get_message(result));

    printf("Listening on %s:%d\n", BIND_ADDRESS, BIND_PORT);

    // seechat_client_t client = {0};
    // EXECUTE_OR_PANIC_ERRNO(client.fd = accept(server.fd, &client.addr.addr, &client.addr_len), client.fd == -1);

    // printf("Accepted a client: %s:%d\n", inet_ntoa(client.addr.addr_in.sin_addr), ntohs(client.addr.addr_in.sin_port));

    // const char* message = "Hello Socket!\n";
    // size_t sent_to_client = send(client.fd, (const void*)message, strlen(message) + 1, 0);

    // printf("Sent %ld bytes to client\n", sent_to_client);

    // EXECUTE_OR_PANIC_ERRNO(int result = close(client.fd), result == -1);

    return EXIT_SUCCESS;
}