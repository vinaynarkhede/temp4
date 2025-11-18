#include "../../include/proxy.h"
#include <zlib.h>

#define CHUNK_SIZE 16384

/* Check if client accepts compression */
int compression_check_accept_encoding(const char *headers) {
    const char *accept_encoding = strcasestr(headers, "Accept-Encoding:");
    if (!accept_encoding) {
        return 0;
    }

    /* Check for gzip */
    if (strcasestr(accept_encoding, "gzip")) {
        return COMPRESS_GZIP;
    }

    /* Check for deflate */
    if (strcasestr(accept_encoding, "deflate")) {
        return COMPRESS_DEFLATE;
    }

    return 0;
}

/* Check if response should be compressed */
int compression_should_compress(const char *content_type, size_t content_length) {
    if (!g_config.enable_compression) {
        return 0;
    }

    /* Don't compress if too small (< 1KB) */
    if (content_length < 1024) {
        return 0;
    }

    /* Don't compress if too large (> 10MB) to avoid memory issues */
    if (content_length > 10 * 1024 * 1024) {
        return 0;
    }

    if (!content_type) {
        return 0;
    }

    /* Compress text-based content */
    const char *compressible_types[] = {
        "text/",
        "application/json",
        "application/javascript",
        "application/xml",
        "application/xhtml",
        NULL
    };

    for (int i = 0; compressible_types[i] != NULL; i++) {
        if (strcasestr(content_type, compressible_types[i])) {
            return 1;
        }
    }

    return 0;
}

/* Compress data using gzip */
int compression_gzip_compress(const unsigned char *input, size_t input_len,
                               unsigned char **output, size_t *output_len) {
    z_stream stream;
    int ret;

    /* Allocate output buffer (worst case: input size + 0.1% + 12 bytes) */
    *output_len = input_len + (input_len / 1000) + 12 + 18; /* +18 for gzip header */
    *output = malloc(*output_len);
    if (!*output) {
        return -1;
    }

    /* Initialize zlib stream for gzip */
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = input_len;
    stream.next_in = (unsigned char *)input;
    stream.avail_out = *output_len;
    stream.next_out = *output;

    /* Use deflateInit2 with gzip encoding (windowBits = 15 + 16) */
    ret = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                       15 + 16, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        free(*output);
        return -1;
    }

    /* Compress */
    ret = deflate(&stream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&stream);
        free(*output);
        return -1;
    }

    *output_len = stream.total_out;
    deflateEnd(&stream);

    return 0;
}

/* Compress data using deflate */
int compression_deflate_compress(const unsigned char *input, size_t input_len,
                                  unsigned char **output, size_t *output_len) {
    z_stream stream;
    int ret;

    /* Allocate output buffer */
    *output_len = input_len + (input_len / 1000) + 12;
    *output = malloc(*output_len);
    if (!*output) {
        return -1;
    }

    /* Initialize zlib stream for deflate */
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = input_len;
    stream.next_in = (unsigned char *)input;
    stream.avail_out = *output_len;
    stream.next_out = *output;

    /* Use deflateInit for raw deflate */
    ret = deflateInit(&stream, Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) {
        free(*output);
        return -1;
    }

    /* Compress */
    ret = deflate(&stream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&stream);
        free(*output);
        return -1;
    }

    *output_len = stream.total_out;
    deflateEnd(&stream);

    return 0;
}

/* Decompress gzip data */
int compression_gzip_decompress(const unsigned char *input, size_t input_len,
                                 unsigned char **output, size_t *output_len) {
    z_stream stream;
    int ret;
    size_t buffer_size = input_len * 4; /* Initial buffer size */

    *output = malloc(buffer_size);
    if (!*output) {
        return -1;
    }

    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = input_len;
    stream.next_in = (unsigned char *)input;
    stream.avail_out = buffer_size;
    stream.next_out = *output;

    /* Initialize for gzip decompression */
    ret = inflateInit2(&stream, 15 + 16);
    if (ret != Z_OK) {
        free(*output);
        return -1;
    }

    /* Decompress */
    ret = inflate(&stream, Z_FINISH);

    /* If output buffer was too small, increase it */
    while (ret == Z_BUF_ERROR) {
        buffer_size *= 2;
        unsigned char *new_output = realloc(*output, buffer_size);
        if (!new_output) {
            inflateEnd(&stream);
            free(*output);
            return -1;
        }
        *output = new_output;

        stream.avail_out = buffer_size - stream.total_out;
        stream.next_out = *output + stream.total_out;

        ret = inflate(&stream, Z_FINISH);
    }

    if (ret != Z_STREAM_END) {
        inflateEnd(&stream);
        free(*output);
        return -1;
    }

    *output_len = stream.total_out;
    inflateEnd(&stream);

    return 0;
}

/* Add Content-Encoding header to response */
int compression_add_encoding_header(char *response, size_t response_size,
                                     const char *encoding) {
    /* Find end of status line */
    char *header_start = strstr(response, "\r\n");
    if (!header_start) {
        return -1;
    }
    header_start += 2;

    /* Check if Content-Encoding already exists */
    if (strcasestr(response, "Content-Encoding:")) {
        return 0; /* Already has encoding header */
    }

    /* Create encoding header */
    char encoding_header[128];
    snprintf(encoding_header, sizeof(encoding_header),
             "Content-Encoding: %s\r\n", encoding);

    /* Calculate new size */
    size_t header_len = strlen(encoding_header);
    size_t response_len = strlen(response);

    if (response_len + header_len >= response_size) {
        return -1; /* Not enough space */
    }

    /* Insert header after status line */
    memmove(header_start + header_len, header_start,
            response_len - (header_start - response) + 1);
    memcpy(header_start, encoding_header, header_len);

    return 0;
}

/* Update Content-Length header */
int compression_update_content_length(char *response, size_t new_length) {
    char *content_length = strcasestr(response, "Content-Length:");
    if (!content_length) {
        return 0; /* No Content-Length header */
    }

    /* Find end of header line */
    char *line_end = strstr(content_length, "\r\n");
    if (!line_end) {
        return -1;
    }

    /* Create new Content-Length header */
    char new_header[128];
    snprintf(new_header, sizeof(new_header), "Content-Length: %zu", new_length);

    /* Calculate lengths */
    size_t old_header_len = line_end - content_length;
    size_t new_header_len = strlen(new_header);

    /* Replace header */
    if (new_header_len <= old_header_len) {
        /* New header fits in old space */
        memcpy(content_length, new_header, new_header_len);
        /* Pad with spaces if shorter */
        for (size_t i = new_header_len; i < old_header_len; i++) {
            content_length[i] = ' ';
        }
    } else {
        /* Need to shift data */
        size_t response_len = strlen(response);
        size_t shift = new_header_len - old_header_len;

        memmove(line_end + shift, line_end, response_len - (line_end - response) + 1);
        memcpy(content_length, new_header, new_header_len);
    }

    return 0;
}

void compression_init(void) {
    if (g_config.enable_compression) {
        printf("[INFO] HTTP compression enabled (gzip, deflate)\n");
    }
}

void compression_cleanup(void) {
    if (g_config.enable_compression) {
        printf("[INFO] Compression system cleaned up\n");
    }
}
