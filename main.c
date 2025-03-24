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

typedef enum seechat_linked_list_result_t
{
    SEECHAT_LINKED_LIST_RESULT_SUCCESS,
    SEECHAT_LINKED_LIST_RESULT_ALLOCATE_FAIL,
    SEECHAT_LINKED_LIST_RESULT_OUT_OUT_BOUNDS,
    SEECHAT_LINKED_LIST_RESULT_SIZE,
} seechat_linked_list_result_t;

struct seechat_linked_list_node_t;
typedef struct seechat_linked_list_node_t
{
    struct seechat_linked_list_node_t* next;
    struct seechat_linked_list_node_t* previous;
    void* data;
} seechat_linked_list_node_t;

typedef struct seechat_linked_list_t
{
    seechat_linked_list_node_t* begin;
    size_t length;
    void(*free_data_function)(void*);
} seechat_linked_list_t;

seechat_linked_list_result_t seechat_linked_list_create(seechat_linked_list_t* list, void(*free_data_function)(void*))
{
    list->length = 0;
    list->begin = NULL;
    list->free_data_function = free_data_function;
    return SEECHAT_LINKED_LIST_RESULT_SUCCESS;
}

seechat_linked_list_result_t seechat_linked_list_append(seechat_linked_list_t* list, void* data)
{
    seechat_linked_list_node_t* element = (seechat_linked_list_node_t*)malloc(sizeof(seechat_linked_list_node_t));
    if (element == NULL) return SEECHAT_LINKED_LIST_RESULT_ALLOCATE_FAIL;
    element->data = data;
    element->next = NULL;
    element->previous = NULL;

    seechat_linked_list_node_t* head = list->begin;

    if (head == NULL) list->begin = element; 
    else
    {
        while (1)
        {
            if (head->next == NULL) break;
            head = head->next;
        }
        head->next = element;
        element->previous = head;
    }
    list->length++;

    return SEECHAT_LINKED_LIST_RESULT_SUCCESS;
}

seechat_linked_list_result_t seechat_linked_list_remove_by_index(seechat_linked_list_t* list, size_t index)
{
    if (index > list->length || index < 0) return SEECHAT_LINKED_LIST_RESULT_OUT_OUT_BOUNDS;
    seechat_linked_list_node_t* element_at_index = list->begin;
    for (size_t i = 0; i < index; ++i) element_at_index = element_at_index->next;

    if (element_at_index->previous != NULL) element_at_index->previous->next = element_at_index->next;
    if (element_at_index->next != NULL) element_at_index->next->previous = element_at_index->previous;
    if (element_at_index == list->begin) list->begin = element_at_index->next;

    if (list->free_data_function != NULL) list->free_data_function(element_at_index->data);
    free(element_at_index);

    list->length--;
    
    return SEECHAT_LINKED_LIST_RESULT_SUCCESS;
}

seechat_linked_list_result_t seechat_linked_list_remove_by_data_pointer(seechat_linked_list_t* list, void* data)
{
    seechat_linked_list_node_t* iterator = list->begin;
    seechat_linked_list_node_t* element = NULL;
    while (iterator != NULL)
    {
        if (iterator->data == data) 
        {
            element = iterator;
            break;
        }
        element = element->next;
    }

    if (element == NULL) return SEECHAT_LINKED_LIST_RESULT_OUT_OUT_BOUNDS;

    if (element->previous != NULL) element->previous->next = element->next;
    if (element->next != NULL) element->next->previous = element->previous;
    if (element == list->begin) list->begin = element->next;

    if (list->free_data_function != NULL) list->free_data_function(element->data);
    free(element);

    list->length--;

    return SEECHAT_LINKED_LIST_RESULT_SUCCESS;
}

void seechat_linked_list_free(seechat_linked_list_t* list)
{
    seechat_linked_list_node_t* head = list->begin;
    while (head != NULL)
    {
        seechat_linked_list_node_t* next = head->next;
        if (list->free_data_function != NULL) list->free_data_function(head->data);
        free(head);
        head = next;
    }
    list->length = 0;
}

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

typedef struct seechat_server_t 
{
    int fd;
    seechat_linked_list_t clients;
} seechat_server_t;

typedef enum seechat_result_t 
{
    SEECHAT_RESULT_SUCCESS,
    SEECHAT_RESULT_SOCKET_FAIL,
    SEECHAT_RESULT_SOCKET_OPTIONS_FAIL,
    SEECHAT_RESULT_IP_CONVERSION_FAIL,
    SEECHAT_RESULT_BIND_FAIL,
    SEECHAT_RESULT_LISTEN_FAIL,
    SEECHAT_RESULT_ACCEPT_FAIL,
    SEECHAT_RESULT_CLOSE_FAIL,
    SEECHAT_RESULT_INIT_FAIL,
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
        case SEECHAT_RESULT_CLOSE_FAIL:
            return strerror(errno);
        case SEECHAT_RESULT_IP_CONVERSION_FAIL:
            return "Invalid IPv4 address";
        case SEECHAT_RESULT_INIT_FAIL:
            return "Failed to initialize the server";
        default:
            return "Missing error message";
    }
}

seechat_result_t seechat_server_create(const char* bind_address, uint16_t bind_port, seechat_server_t* server) 
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

    if (seechat_linked_list_create(&server->clients, free) != SEECHAT_LINKED_LIST_RESULT_SUCCESS) return SEECHAT_RESULT_INIT_FAIL;

    return SEECHAT_RESULT_SUCCESS;
}

void seechat_server_free(seechat_server_t* server)
{
    seechat_linked_list_free(&server->clients);
}

seechat_result_t seechat_server_listen(seechat_server_t* server, void(*client_callback)(seechat_client_t*)) 
{
    int result;
    seechat_client_t client = {0};

    result = accept(server->fd, &client.addr.addr, &client.addr_len);
    if (result == -1) return SEECHAT_RESULT_ACCEPT_FAIL;

    client.fd = result;
    client_callback(&client);

    result = close(client.fd);
    if (result == -1) return SEECHAT_RESULT_CLOSE_FAIL;

    return SEECHAT_RESULT_SUCCESS;
}

static void seechat_client_callback(seechat_client_t* client)
{
    printf("Accepted a client: %s:%d\n", inet_ntoa(client->addr.addr_in.sin_addr), ntohs(client->addr.addr_in.sin_port));
    const char* message = "Hello Socket!\n";
    size_t sent_to_client = send(client->fd, (const void*)message, strlen(message) + 1, 0);
    printf("Sent %ld bytes to client\n", sent_to_client);
}

int main(void)
{
    seechat_server_t server;
    EXECUTE_OR_PANIC(seechat_result_t result = seechat_server_create(BIND_ADDRESS, BIND_PORT, &server), result != SEECHAT_RESULT_SUCCESS, seechat_result_get_message(result));
    EXECUTE_OR_PANIC(seechat_result_t result = seechat_server_listen(&server, seechat_client_callback), result != SEECHAT_RESULT_SUCCESS, seechat_result_get_message(result));
    seechat_server_free(&server);
    return EXIT_SUCCESS;
}