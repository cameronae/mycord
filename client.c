#include <stdbool.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <ctype.h>
#include <stdint.h>
#include <termios.h>

#include "tui.h"

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

/* ==================== Message Queue ==================== */
typedef struct {
    message_t messages[SEND_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
} send_queue_t;

/* ==================== Global State ==================== */
static bool running = true;
static settings_t settings = {0};
static send_queue_t send_queue = {0};

/* TUI Globals */
static tui_state_t g_tui;
static pthread_mutex_t g_tui_lock = PTHREAD_MUTEX_INITIALIZER;


#include <termios.h>


/* ==================== Helper Functions ==================== */

void set_raw_mode(bool enable) {
    static struct termios oldt, newt;
    if (enable) {
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO); // Disable buffering and echoing
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }
}

void print_help(void) {
    fprintf(stdout, "usage ./client [-h] [--port PORT] [--ip IP] [--domain DOMAIN] [--quiet]\n\n");
    fprintf(stdout, "mycord client\n\n");
    fprintf(stdout, "options:\n");
    fprintf(stdout, "  --help         show this help message and exit\n");
    fprintf(stdout, "  --port PORT    port to connect to (default: 8080)\n");
    fprintf(stdout, "  --ip IP        IP address to connect to (if domain is specified, IP must not be)\n");
    fprintf(stdout, "  --domain DOMAIN  Domain name to connect to (default: none)\n");
    fprintf(stdout, "  --quiet        do not perform alerts or mention highlighting\n");
    fprintf(stdout, "  --tui          create a text interface\n\n");
    fprintf(stdout, "examples:\n");
    fprintf(stdout, "  ./client --help\n");
    fprintf(stdout, "  ./client --port 1738\n");
    fprintf(stdout, "  ./client --domain example.com\n");
}

int h_dns_lookup(const char* domain, client_config_t* config) {
    struct hostent* result = gethostbyname(domain);
    if (result == NULL) {
        fprintf(stderr, "Error: DNS lookup failed for domain '%s'\n", domain);
        return -1;
    }

    uint32_t ip_int = ntohl(*(uint32_t*)result->h_addr);
    int byte_0 = (ip_int >> 0) & 0xFF;
    int byte_1 = (ip_int >> 8) & 0xFF;
    int byte_2 = (ip_int >> 16) & 0xFF;
    int byte_3 = (ip_int >> 24) & 0xFF;

    snprintf(config->ip, MAX_IP_LEN, "%d.%d.%d.%d", byte_3, byte_2, byte_1, byte_0);
    return 0;
}

int h_get_username(char* dest, size_t n) {
    const char* user = getenv("USER");
    if (user == NULL || user[0] == '\0') {
        fprintf(stderr, "Error: USERNAME environment variable is empty\n");
        return -1;
    }

    strncpy(dest, user, n - 1);
    dest[n - 1] = '\0';

    for (size_t i = 0; i < strlen(dest); i++) {
        if (!isprint(dest[i])) {
            fprintf(stderr, "Error: username contains non-printable characters\n");
            return -1;
        }
    }
    return 0;
}

int h_process_args(int argc, char* argv[], client_config_t* config, settings_t* settings) {
    bool ip_set = false;
    bool domain_set = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help();
            exit(0);
        }
        else if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) {
                config->port = (uint32_t)atoi(argv[i + 1]);
                i++;
            } else {
                fprintf(stderr, "Error: --port requires a port number\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--ip") == 0) {
            if (i + 1 < argc) {
                if (domain_set) {
                    fprintf(stderr, "Error: Cannot specify both --ip and --domain\n");
                    return -1;
                }
                strncpy(config->ip, argv[i + 1], MAX_IP_LEN - 1);
                config->ip[MAX_IP_LEN - 1] = '\0';
                ip_set = true;
                i++;
            } else {
                fprintf(stderr, "Error: --ip requires an IP address\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--domain") == 0) {
            if (i + 1 < argc) {
                if (ip_set) {
                    fprintf(stderr, "Error: Cannot specify both --ip and --domain\n");
                    return -1;
                }
                strncpy(config->domain, argv[i + 1], MAX_DOMAIN_LEN - 1);
                config->domain[MAX_DOMAIN_LEN - 1] = '\0';
                domain_set = true;
                if (h_dns_lookup(config->domain, config) != 0) {
                    return -1;
                }
                i++;
            } else {
                fprintf(stderr, "Error: --domain requires a domain name\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--quiet") == 0) {
            settings->quiet = true;
        }
        else if (strcmp(argv[i], "--tui") == 0) {
            settings->tui = true;
        }
        else {
            fprintf(stderr, "Error: unknown argument '%s'\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

int h_send_message(const message_t* msg) {
    const char* buf = (const char*)msg;
    size_t total = sizeof(message_t);
    size_t sent = 0;

    while (sent < total) {
        ssize_t n = write(settings.socket_fd, buf + sent, total - sent);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (running) {
                fprintf(stderr, "Error: write failed [%s]\n", strerror(errno));
            }
            return -1;
        }
        if (n == 0) {
            fprintf(stderr, "Error: write returned 0 (connection closed)\n");
            return -1;
        }
        sent += n;
    }
    return 0;
}

int h_recv_message(message_t* msg) {
    char* buf = (char*)msg;
    size_t total = sizeof(message_t);
    size_t received = 0;

    while (received < total) {
        ssize_t n = read(settings.socket_fd, buf + received, total - received);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (running) {
                fprintf(stderr, "Error: read failed [%s]\n", strerror(errno));
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        received += n;
    }
    return 0;
}

int h_validate_message(const char* msg, size_t len) {
    if (len < 1 || len > MESSAGE_LEN - 1) {
        fprintf(stderr, "Error: Message must be 1-%d characters\n", MESSAGE_LEN - 1);
        fflush(stderr);
        return -1;
    }

    for (size_t i = 0; i < len; i++) {
        if (!isprint(msg[i])) {
            fprintf(stderr, "Error: Message contains non-printable characters\n");
            return -1;
        }
    }
    return 0;
}

void h_parse_message_highlights(const char* message, const char* mention_token) {
    const char* curr_pos = message;
    const char* word_match = strstr(message, mention_token);

    while (word_match != NULL) {
        fwrite(curr_pos, sizeof(char), word_match - curr_pos, stdout);
        fprintf(stdout, "\a%s%s%s", COLOR_RED, mention_token, COLOR_RESET);
        curr_pos = word_match + strlen(mention_token);
        word_match = strstr(curr_pos, mention_token);
    }
    fwrite(curr_pos, sizeof(char), strlen(curr_pos), stdout);
    fprintf(stdout, "\n");
}

void h_queue_init(send_queue_t* queue) {
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    pthread_mutex_init(&queue->lock, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
}

int h_queue_enqueue(send_queue_t* queue, const message_t* msg) {
    pthread_mutex_lock(&queue->lock);

    if (queue->count >= SEND_QUEUE_SIZE) {
        pthread_mutex_unlock(&queue->lock);
        fprintf(stderr, "Error: send queue is full\n");
        return -1;
    }

    queue->messages[queue->tail] = *msg;
    queue->tail = (queue->tail + 1) % SEND_QUEUE_SIZE;
    queue->count++;

    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->lock);
    return 0;
}

int h_queue_dequeue(send_queue_t* queue, message_t* msg) {
    pthread_mutex_lock(&queue->lock);

    while (queue->count == 0 && running) {
        pthread_cond_wait(&queue->not_empty, &queue->lock);
    }

    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    *msg = queue->messages[queue->head];
    queue->head = (queue->head + 1) % SEND_QUEUE_SIZE;
    queue->count--;

    pthread_mutex_unlock(&queue->lock);
    return 0;
}

int h_send_logout(void) {
    message_t logout_msg = {.type = htonl(MSG_LOGOUT)};
    return h_send_message(&logout_msg);
}

void h_handle_signal(int signal) {
    running = false;
    pthread_cond_signal(&send_queue.not_empty);
}

void* h_receive_messages_thread(void* arg) {
    message_t msg;
    char mention_token[MENTION_PREFIX_LEN];
    snprintf(mention_token, sizeof(mention_token), "@%s", settings.username);

    while (running) {
        if (h_recv_message(&msg) != 0) {
            break;
        }

        uint32_t msg_type = ntohl(msg.type);

        // Format timestamp
        char time_str_full[20];
        char time_str_short[10];
        time_t timestamp = (time_t)ntohl(msg.timestamp);
        struct tm* time_info = localtime(&timestamp);
        strftime(time_str_full, sizeof(time_str_full), "%Y-%m-%d %H:%M:%S", time_info);
        strftime(time_str_short, sizeof(time_str_short), "%H:%M", time_info);

        if (settings.tui) {
            /* TUI MODE RECEIVE */
            pthread_mutex_lock(&g_tui_lock);
            
            if (msg_type == MSG_MESSAGE_RECV) {
                 if (strcmp(msg.username, settings.username) != 0) { //THIS GETS RID OF THE "ECHO" 
                    tui_add_message(&g_tui, TUI_MSG_CHAT, time_str_short, msg.username, msg.message);
                }
            } else if (msg_type == MSG_SYSTEM) {
                tui_add_message(&g_tui, TUI_MSG_SYSTEM, time_str_short, "SYSTEM", msg.message);
            } else if (msg_type == MSG_DISCONNECT) {
                tui_add_message(&g_tui, TUI_MSG_DISCONNECT, time_str_short, "SERVER", msg.message);
                tui_set_disconnected(&g_tui);
                running = false;
            }
            
            pthread_mutex_unlock(&g_tui_lock);
        } else {
            /* STANDARD CLI RECEIVE */
            switch (msg_type) {
                case MSG_MESSAGE_RECV:
                    fprintf(stdout, "[%s] %s: ", time_str_full, msg.username);
                    if (!settings.quiet) {
                        h_parse_message_highlights(msg.message, mention_token);
                    } else {
                        if (strcmp(msg.username, settings.username) != 0) { //only prints once if its your username
                        fprintf(stdout, "%s\n", msg.message);
                        }
                    }
                    break;
                case MSG_SYSTEM:
                    fprintf(stdout, "%s[SYSTEM] %s%s\n", COLOR_GRAY, msg.message, COLOR_RESET);
                    break;
                case MSG_DISCONNECT:
                    fprintf(stdout, "%s[DISCONNECT] %s%s\n", COLOR_RED, msg.message, COLOR_RESET);
                    running = false;
                    break;
                default:
                    fprintf(stderr, "Error: unknown message type %u\n", msg_type);
                    running = false;
                    break;
            }
        }
    }
    return NULL;
}

void* h_send_messages_thread(void* arg) {
    message_t msg;

    while (running) {
        if (h_queue_dequeue(&send_queue, &msg) != 0) {
            break;
        }

        if (h_send_message(&msg) != 0) {
            running = false;
            break;
        }
    }
    return NULL;
}

/* ==================== Main ==================== */

int main(int argc, char* argv[]) {
    /* Setup signal handlers */
    struct sigaction sa;
    sa.sa_handler = h_handle_signal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Initialize config with defaults */
    client_config_t config = {
        .ip = DEFAULT_IP,
        .port = DEFAULT_PORT,
        .domain = ""
    };

    /* Parse command-line arguments */
    if (h_process_args(argc, argv, &config, &settings) != 0) {
        fprintf(stderr, "Error: argument parsing failed\n");
        return 1;
    }

    /* Get username */
    if (h_get_username(settings.username, sizeof(settings.username)) != 0) {
        return 1;
    }

    /* Create socket */
    settings.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (settings.socket_fd == -1) {
        fprintf(stderr, "Error: socket creation failed [%s]\n", strerror(errno));
        return 1;
    }

    /* Prepare server address */
    settings.server.sin_family = AF_INET;
    settings.server.sin_port = htons(config.port);
    if (inet_pton(AF_INET, config.ip, &settings.server.sin_addr) != 1) {
        fprintf(stderr, "Error: invalid IP address '%s'\n", config.ip);
        close(settings.socket_fd);
        return 1;
    }

    /* Connect to server */
    if (connect(settings.socket_fd, (struct sockaddr*)&settings.server,
                sizeof(settings.server)) == -1) {
        fprintf(stderr, "Error: connection failed [%s]\n", strerror(errno));
        close(settings.socket_fd);
        return 1;
    }

    /* Send login message */
    message_t login_msg = {.type = htonl(MSG_LOGIN)};
    strncpy(login_msg.username, settings.username, USERNAME_LEN - 1);
    login_msg.username[USERNAME_LEN - 1] = '\0';

    if (h_send_message(&login_msg) != 0) {
        close(settings.socket_fd);
        return 1;
    }

    /* Create receive thread */
    pthread_t recv_thread;
    if (pthread_create(&recv_thread, NULL, h_receive_messages_thread, NULL) != 0) {
        fprintf(stderr, "Error: thread creation failed [%s]\n", strerror(errno));
        close(settings.socket_fd);
        return 1;
    }

    /* Initialize and create send queue thread */
    h_queue_init(&send_queue);
    pthread_t send_thread;
    if (pthread_create(&send_thread, NULL, h_send_messages_thread, NULL) != 0) {
        fprintf(stderr, "Error: send thread creation failed [%s]\n", strerror(errno));
        close(settings.socket_fd);
        return 1;
    }

    /* Main input loop */
    message_t send_msg = {.type = htonl(MSG_MESSAGE_SEND)};
    strncpy(send_msg.username, settings.username, USERNAME_LEN - 1); //needs username to fix echo

    if (settings.tui) {
        // --- TUI LOOP ---
        tui_init(&g_tui, settings.username, settings.quiet);
        set_raw_mode(true);

        while (running) {
            int ch = getchar();
            
            if (ch == EOF) break;
            if (ch == 3 || ch == 4) { // Ctrl+C and Ctrl+D
                h_send_logout();
                break;
            }

            pthread_mutex_lock(&g_tui_lock);
            
            if (ch == '\033') {
                // Handle Escape sequences (Arrows, etc.)
                if (getchar() == '[') {
                    tui_handle_escape(&g_tui, getchar());
                }
            } else {
                // Handle normal typing
                const char* input_buf = tui_handle_key(&g_tui, ch);
                
                if (input_buf != NULL) {
                    size_t len = strlen(input_buf);
                    
                    if (len > 0 && len < MESSAGE_LEN) {
                        // Display our own message instantly in the TUI
                        time_t now = time(NULL);
                        char time_str[20];
                        strftime(time_str, sizeof(time_str), "%H:%M", localtime(&now));
                        tui_add_message(&g_tui, TUI_MSG_OWN, time_str, settings.username, input_buf);

                        // Queue it for sending
                        memset(send_msg.message, 0, MESSAGE_LEN);
                        strncpy(send_msg.message, input_buf, MESSAGE_LEN - 1);
                        h_queue_enqueue(&send_queue, &send_msg);
                    }
                }
            }
            
            pthread_mutex_unlock(&g_tui_lock);
        }

        set_raw_mode(false);
        tui_cleanup(&g_tui);

    } else {
        // --- STANDARD CLI LOOP ---
        char input_buf[2048];  
        
        while (running) {
            fprintf(stdout, "You: ");
            fflush(stdout);

            if (fgets(input_buf, sizeof(input_buf), stdin) == NULL) {
                h_send_logout();
                break;
            }

            size_t len = strlen(input_buf);

            if (len == sizeof(input_buf) - 1 && input_buf[len - 1] != '\n') {
                fprintf(stderr, "Error: Message is too long.\n");
                int ch;
                while ((ch = getchar()) != '\n' && ch != EOF);
                continue; 
            }

            if (len > 0 && input_buf[len - 1] == '\n') {
                input_buf[--len] = '\0';
            }

            if (len >= MESSAGE_LEN) {
                fprintf(stderr, "Error: Message exceeds %d characters.\n", MESSAGE_LEN - 1);
                continue;
            }

            if (h_validate_message(input_buf, len) != 0) continue;

            memset(send_msg.message, 0, MESSAGE_LEN);
            strncpy(send_msg.message, input_buf, MESSAGE_LEN - 1);

            if (h_queue_enqueue(&send_queue, &send_msg) != 0) {
                break;
            }
        }
    }

    /* Cleanup */
    running = false;
    pthread_cond_signal(&send_queue.not_empty);
     //Force-kill the socket to unblock the recv_thread
    shutdown(settings.socket_fd, SHUT_RDWR);

    if (pthread_join(send_thread, NULL) != 0) {
        fprintf(stderr, "Warning: pthread_join(send_thread) failed\n");
    }
    if (pthread_join(recv_thread, NULL) != 0) {
        fprintf(stderr, "Warning: pthread_join(recv_thread) failed\n");
    }
    close(settings.socket_fd);
    return 0;
}
