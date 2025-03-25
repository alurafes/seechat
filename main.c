#include <stdio.h>
#include <sys/socket.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

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

#define seechat_linked_list_foreach(list_ptr, action)\
{\
    seechat_linked_list_node_t* iterator = (list_ptr)->begin;\
    while (iterator != NULL)\
    {\
        {action}\
        iterator = iterator->next;\
    }\
}

#define SEECHAT_STRING_BUILDER_INITIAL_CAPACITY (64)

typedef enum seechat_string_builder_result_t
{
    SEECHAT_STRING_BUILDER_RESULT_SUCCESS,
    SEECHAT_STRING_BUILDER_RESULT_ALLOCATE_FAIL,
    SEECHAT_STRING_BUILDER_RESULT_SIZE
} seechat_string_builder_result_t;

typedef struct seechat_string_builder_t 
{
    char* data;
    size_t length;
    size_t capacity;
} seechat_string_builder_t;

seechat_string_builder_result_t seechat_string_builder_create(seechat_string_builder_t* string_builder)
{
    string_builder->capacity = SEECHAT_STRING_BUILDER_INITIAL_CAPACITY;
    string_builder->length = 0;
    string_builder->data = (char*)malloc(SEECHAT_STRING_BUILDER_INITIAL_CAPACITY);
    if (string_builder->data == NULL) return SEECHAT_STRING_BUILDER_RESULT_ALLOCATE_FAIL;
    string_builder->data[0] = '\0';
    return SEECHAT_STRING_BUILDER_RESULT_SUCCESS;
}

seechat_string_builder_result_t seechat_string_builder_append(seechat_string_builder_t* string_builder, char* string)
{
    size_t string_length = strlen(string);

    size_t required_capacity = string_builder->length + string_length + 1;
    if (required_capacity > string_builder->capacity) {
        string_builder->capacity = required_capacity * 2;
        string_builder->data = realloc(string_builder->data, string_builder->capacity);
        if (string_builder->data == NULL) return SEECHAT_STRING_BUILDER_RESULT_ALLOCATE_FAIL;
    }

    strcpy(string_builder->data + string_builder->length, string);
    string_builder->length += string_length;

    return SEECHAT_STRING_BUILDER_RESULT_SUCCESS;
}

void seechat_string_builder_clear(seechat_string_builder_t* string_builder)
{
    string_builder->length = 0;
    string_builder->data[0] = '\0';
}

void seechat_string_builder_free(seechat_string_builder_t* string_builder)
{
    free(string_builder->data);
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
    struct pollfd poll_fd;
    seechat_string_builder_t read_buffer_sb;
} seechat_client_t;

typedef struct seechat_server_t 
{
    int fd;
    struct pollfd poll_fd;
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
    SEECHAT_RESULT_SET_NONBLOCK_FAIL,
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
        case SEECHAT_RESULT_SET_NONBLOCK_FAIL:
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

    result = fcntl(server->fd, F_SETFL, fcntl(server->fd, F_GETFL, 0) | O_NONBLOCK);
    if (result == -1) return SEECHAT_RESULT_SOCKET_FAIL;

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

    server->poll_fd.fd = server->fd;
    server->poll_fd.events = POLLIN;

    return SEECHAT_RESULT_SUCCESS;
}

void seechat_server_close_clients(seechat_server_t* server)
{
    seechat_linked_list_node_t* head = server->clients.begin;
    while (head != NULL)
    {
        seechat_client_t* client = (seechat_client_t*)head->data;
        close(client->fd);
        seechat_string_builder_free(&client->read_buffer_sb);
        head = head->next;
    }
}

void seechat_server_free(seechat_server_t* server)
{
    seechat_server_close_clients(server);
    seechat_linked_list_free(&server->clients);
}

seechat_result_t seechat_server_accept(seechat_server_t* server) 
{
    int result;
    seechat_client_t* client = (seechat_client_t*)calloc(1, sizeof(seechat_client_t));

    result = accept(server->fd, &client->addr.addr, (socklen_t*)&client->addr_len);
    if (result == -1) return SEECHAT_RESULT_ACCEPT_FAIL;

    client->fd = result;
    seechat_linked_list_append(&server->clients, (void*)client);

    result = fcntl(client->fd, F_SETFL, fcntl(client->fd, F_GETFL, 0) | O_NONBLOCK);
    if (result == -1) return SEECHAT_RESULT_SOCKET_FAIL;

    client->poll_fd.fd = client->fd;
    client->poll_fd.events = POLLIN | POLLOUT | POLLHUP;

    EXECUTE_OR_PANIC(seechat_string_builder_result_t result = seechat_string_builder_create(&client->read_buffer_sb), result != SEECHAT_STRING_BUILDER_RESULT_SUCCESS, "failed to initialize a client");

    printf("Acceped a client %s:%d\n", inet_ntoa(client->addr.addr_in.sin_addr), ntohs(client->addr.addr_in.sin_port));

    return SEECHAT_RESULT_SUCCESS;
}

static void seechat_client_callback(seechat_server_t* server, seechat_client_t* client)
{
    EXECUTE_OR_PANIC_ERRNO(int activity = poll(&client->poll_fd, 1, 0), activity == -1);
    char buffer[64] = {0};
    int read_bytes = 0;

    if (client->poll_fd.revents & POLLHUP)
    {
        // todo: kill client
        return;
    }

    if (client->poll_fd.revents & POLLIN)
    {
        seechat_string_builder_clear(&client->read_buffer_sb);
        while ((read_bytes = recv(client->fd, buffer, 64, 0)) > 0)
        {
            seechat_string_builder_append(&client->read_buffer_sb, buffer);
            memset(buffer, 0, 64);
        }
        printf("data (%zu): %s\n", client->read_buffer_sb.length, client->read_buffer_sb.data);
    }
}

int main(void)
{
    seechat_server_t server;
    EXECUTE_OR_PANIC(seechat_result_t result = seechat_server_create(BIND_ADDRESS, BIND_PORT, &server), result != SEECHAT_RESULT_SUCCESS, seechat_result_get_message(result));

    while (1) // todo: listen to ^Z or something
    {
        EXECUTE_OR_PANIC_ERRNO(int activity = poll(&server.poll_fd, 1, 0), activity == -1)
        if (server.poll_fd.revents & POLLIN)
        {
            EXECUTE_OR_PANIC(seechat_result_t result = seechat_server_accept(&server), result != SEECHAT_RESULT_SUCCESS, seechat_result_get_message(result));
        }

        seechat_linked_list_foreach(&server.clients, {
            seechat_client_callback(&server, (seechat_client_t*)iterator->data);
        });
    }

    seechat_server_free(&server);
    return EXIT_SUCCESS;
}