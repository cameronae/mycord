#ifdef CLIENT_H
#define CLIENT_H



void parse_args(int argc, char* argv[], client_config_t* config);

int dns_lookup(const char* domain, client_config_t* config);


/* ==================== Constants ==================== */
#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 8080
#define MAX_IP_LEN 16
#define MAX_DOMAIN_LEN 254
#define USERNAME_LEN 32
#define MESSAGE_LEN 1024
#define MENTION_PREFIX_LEN (USERNAME_LEN + 1)  // '@' + username

#define COLOR_RED "\033[31m"
#define COLOR_GRAY "\033[90m"
#define COLOR_RESET "\033[0m"
#define SEND_QUEUE_SIZE 100

/* ==================== Message Type Enum ==================== */
typedef enum MessageType {
    MSG_LOGIN = 0,
    MSG_LOGOUT = 1,
    MSG_MESSAGE_SEND = 2,
    MSG_MESSAGE_RECV = 10,
    MSG_DISCONNECT = 12,
    MSG_SYSTEM = 13
} message_type_t;

/* ==================== Message Structure ==================== */
typedef struct __attribute__((packed)) Message {
    uint32_t type;
    uint32_t timestamp;
    char username[USERNAME_LEN];
    char message[MESSAGE_LEN];
} message_t;

/* ==================== Configuration ==================== */
typedef struct Config {
    char ip[MAX_IP_LEN];
    uint32_t port;
    char domain[MAX_DOMAIN_LEN];
} client_config_t;

typedef struct Settings {
    struct sockaddr_in server;
    bool quiet;
    int socket_fd;
    char username[USERNAME_LEN];
    bool tui;
} settings_t;

#endif 