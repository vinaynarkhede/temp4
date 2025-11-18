#include "../../include/proxy.h"
#include <ctype.h>

/* User credentials structure */
struct auth_credentials {
    char username[128];
    char password[128];
    struct auth_credentials *next;
};

static auth_credentials_t *g_users = NULL;
static pthread_mutex_t g_auth_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Base64 decoding table */
static const unsigned char base64_decode_table[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

static int base64_decode(const char *input, char *output, size_t output_size) {
    size_t input_len = strlen(input);
    size_t output_len = 0;
    unsigned char buf[4];
    int i = 0, j = 0;

    while (input_len--) {
        if (*input == '=') {
            break;
        }

        unsigned char c = base64_decode_table[(unsigned char)*input++];
        if (c == 64) {
            continue;
        }

        buf[i++] = c;

        if (i == 4) {
            if (output_len + 3 >= output_size) {
                return -1;
            }

            output[output_len++] = (buf[0] << 2) | (buf[1] >> 4);
            output[output_len++] = (buf[1] << 4) | (buf[2] >> 2);
            output[output_len++] = (buf[2] << 6) | buf[3];
            i = 0;
        }
    }

    if (i > 0) {
        if (output_len + i >= output_size) {
            return -1;
        }

        if (i >= 2) {
            output[output_len++] = (buf[0] << 2) | (buf[1] >> 4);
        }
        if (i >= 3) {
            output[output_len++] = (buf[1] << 4) | (buf[2] >> 2);
        }
    }

    output[output_len] = '\0';
    return output_len;
}

int auth_init(const char *auth_file) {
    if (!auth_file) {
        /* Create a default user for testing */
        auth_credentials_t *user = malloc(sizeof(auth_credentials_t));
        if (user) {
            strcpy(user->username, "admin");
            strcpy(user->password, "admin");
            user->next = NULL;
            g_users = user;
            printf("[INFO] Auth initialized with default user (admin/admin)\n");
        }
        return 0;
    }

    FILE *fp = fopen(auth_file, "r");
    if (!fp) {
        fprintf(stderr, "[WARN] Cannot open auth file: %s, using default user\n",
                auth_file);
        /* Create default user */
        auth_credentials_t *user = malloc(sizeof(auth_credentials_t));
        if (user) {
            strcpy(user->username, "admin");
            strcpy(user->password, "admin");
            user->next = NULL;
            g_users = user;
        }
        return 0;
    }

    char line[512];
    int count = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline */
        line[strcspn(line, "\n")] = 0;

        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }

        /* Parse username:password */
        char *colon = strchr(line, ':');
        if (!colon) {
            continue;
        }

        *colon = '\0';
        char *username = line;
        char *password = colon + 1;

        /* Create credentials entry */
        auth_credentials_t *cred = malloc(sizeof(auth_credentials_t));
        if (!cred) {
            continue;
        }

        strncpy(cred->username, username, sizeof(cred->username) - 1);
        strncpy(cred->password, password, sizeof(cred->password) - 1);
        cred->next = g_users;
        g_users = cred;
        count++;
    }

    fclose(fp);
    printf("[INFO] Loaded %d user(s) from %s\n", count, auth_file);
    return 0;
}

int auth_verify(const char *username, const char *password) {
    if (!username || !password) {
        return -1;
    }

    pthread_mutex_lock(&g_auth_mutex);

    auth_credentials_t *user = g_users;
    while (user) {
        if (strcmp(user->username, username) == 0 &&
            strcmp(user->password, password) == 0) {
            pthread_mutex_unlock(&g_auth_mutex);
            return 0;
        }
        user = user->next;
    }

    pthread_mutex_unlock(&g_auth_mutex);
    return -1;
}

int auth_parse_basic(const char *auth_header, char *username, char *password) {
    if (!auth_header || !username || !password) {
        return -1;
    }

    /* Find "Basic " prefix */
    const char *basic = strcasestr(auth_header, "Basic ");
    if (!basic) {
        return -1;
    }

    basic += 6; /* Skip "Basic " */

    /* Skip whitespace */
    while (*basic && isspace(*basic)) {
        basic++;
    }

    /* Decode base64 */
    char decoded[512];
    if (base64_decode(basic, decoded, sizeof(decoded)) < 0) {
        return -1;
    }

    /* Parse username:password */
    char *colon = strchr(decoded, ':');
    if (!colon) {
        return -1;
    }

    *colon = '\0';
    strcpy(username, decoded);
    strcpy(password, colon + 1);

    return 0;
}

void auth_cleanup(void) {
    pthread_mutex_lock(&g_auth_mutex);

    auth_credentials_t *user = g_users;
    while (user) {
        auth_credentials_t *next = user->next;
        free(user);
        user = next;
    }

    g_users = NULL;

    pthread_mutex_unlock(&g_auth_mutex);

    printf("[INFO] Auth system cleaned up\n");
}
