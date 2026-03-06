#include "httpd.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <3ds.h>

#define SOC_ALIGN       0x1000
#define SOC_BUFFERSIZE  0x100000
#define RECV_BUF_SIZE   (32 * 1024)

static u32* soc_buffer = NULL;
static bool soc_initialized = false;

// Upload page HTML (embedded to avoid RomFS dependency during dev)
static const char upload_html[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>3DS EPUB Upload</title>"
    "<style>"
    "*{box-sizing:border-box}"
    "body{font-family:sans-serif;max-width:500px;margin:40px auto;padding:20px;"
    "background:#1a1a2e;color:#faf8f0}"
    "h1{color:#4a90d9;margin-bottom:8px}"
    "p.s{color:#888;margin-top:0}"
    "form{background:#2a2a4a;padding:24px;border-radius:8px;border:1px solid #3a3a5c}"
    "input[type=file]{width:100%;padding:12px;margin:12px 0;background:#1a1a2e;"
    "color:#faf8f0;border:1px dashed #4a90d9;border-radius:4px}"
    "input[type=submit]{width:100%;padding:12px;font-size:16px;background:#4a90d9;"
    "color:#fff;border:none;border-radius:4px;cursor:pointer}"
    "input[type=submit]:hover{background:#3a70b0}"
    ".ok{color:#4a9;padding:16px;text-align:center}"
    "</style></head><body>"
    "<h1>3DS EPUB Upload</h1>"
    "<p class='s'>Upload EPUB files to your 3DS library</p>"
    "<form method='POST' enctype='multipart/form-data' action='/upload'>"
    "<input type='file' name='epub' accept='.epub' required>"
    "<input type='submit' value='Upload to 3DS'>"
    "</form></body></html>";

static const char upload_ok_html[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Upload OK</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:500px;margin:40px auto;padding:20px;"
    "background:#1a1a2e;color:#faf8f0;text-align:center}"
    "h1{color:#4a9}"
    "a{color:#4a90d9}"
    "</style></head><body>"
    "<h1>Upload successful!</h1>"
    "<p><a href='/'>Upload another</a></p>"
    "</body></html>";

static bool init_soc(void) {
    if (soc_initialized) return true;

    soc_buffer = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (!soc_buffer) return false;

    if (socInit(soc_buffer, SOC_BUFFERSIZE) != 0) {
        free(soc_buffer);
        soc_buffer = NULL;
        return false;
    }

    soc_initialized = true;
    return true;
}

bool httpd_init(HttpServer* srv, int port) {
    memset(srv, 0, sizeof(HttpServer));
    srv->server_sock = -1;
    srv->port = port;
    strcpy(srv->status_msg, "Initializing...");

    if (!init_soc()) {
        strcpy(srv->status_msg, "Failed to init network");
        return false;
    }

    // Get IP address
    u32 ip = gethostid();
    struct in_addr addr;
    addr.s_addr = ip;
    strncpy(srv->ip_str, inet_ntoa(addr), sizeof(srv->ip_str) - 1);

    // Create socket
    srv->server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->server_sock < 0) {
        strcpy(srv->status_msg, "Failed to create socket");
        return false;
    }

    // Set non-blocking for accept
    int flags = fcntl(srv->server_sock, F_GETFL, 0);
    fcntl(srv->server_sock, F_SETFL, flags | O_NONBLOCK);

    // Allow reuse
    int yes = 1;
    setsockopt(srv->server_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // Bind
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(srv->server_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        strcpy(srv->status_msg, "Failed to bind");
        close(srv->server_sock);
        srv->server_sock = -1;
        return false;
    }

    if (listen(srv->server_sock, 1) < 0) {
        strcpy(srv->status_msg, "Failed to listen");
        close(srv->server_sock);
        srv->server_sock = -1;
        return false;
    }

    srv->running = true;
    snprintf(srv->status_msg, sizeof(srv->status_msg),
             "Ready at http://%s:%d", srv->ip_str, srv->port);
    return true;
}

// Read headers from client, return header length (including \r\n\r\n)
static int read_headers(int sock, char* buf, int buf_size) {
    int total = 0;
    while (total < buf_size - 1) {
        int n = recv(sock, buf + total, 1, 0);
        if (n <= 0) return -1;
        total++;
        if (total >= 4 &&
            buf[total-4] == '\r' && buf[total-3] == '\n' &&
            buf[total-2] == '\r' && buf[total-1] == '\n') {
            buf[total] = '\0';
            return total;
        }
    }
    return -1;
}

// Extract a header value (case-insensitive key search)
static bool get_header(const char* headers, const char* key, char* value, int value_size) {
    const char* p = headers;
    int key_len = strlen(key);

    while (*p) {
        if (strncasecmp(p, key, key_len) == 0 && p[key_len] == ':') {
            p += key_len + 1;
            while (*p == ' ') p++;
            int i = 0;
            while (*p && *p != '\r' && *p != '\n' && i < value_size - 1) {
                value[i++] = *p++;
            }
            value[i] = '\0';
            return true;
        }
        // Skip to next line
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return false;
}

// Parse boundary from Content-Type header
static bool get_boundary(const char* content_type, char* boundary, int boundary_size) {
    const char* b = strstr(content_type, "boundary=");
    if (!b) return false;
    b += 9;

    // Skip optional quotes
    if (*b == '"') b++;

    int i = 0;
    while (*b && *b != '"' && *b != '\r' && *b != '\n' && *b != ';' && i < boundary_size - 1) {
        boundary[i++] = *b++;
    }
    boundary[i] = '\0';
    return i > 0;
}

// Extract filename from Content-Disposition header in multipart part
static bool get_filename(const char* part_header, char* filename, int filename_size) {
    const char* fn = strstr(part_header, "filename=\"");
    if (!fn) return false;
    fn += 10;

    int i = 0;
    while (*fn && *fn != '"' && i < filename_size - 1) {
        filename[i++] = *fn++;
    }
    filename[i] = '\0';

    // Sanitize: only keep the basename
    char* slash = strrchr(filename, '/');
    if (slash) memmove(filename, slash + 1, strlen(slash + 1) + 1);
    slash = strrchr(filename, '\\');
    if (slash) memmove(filename, slash + 1, strlen(slash + 1) + 1);

    return filename[0] != '\0';
}

static void serve_page(int sock, const char* html) {
    char header[256];
    int body_len = strlen(html);
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n", body_len);
    send(sock, header, hlen, 0);
    send(sock, html, body_len, 0);
}

static void serve_error(int sock, int code, const char* msg) {
    char response[512];
    int len = snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n%s",
        code, msg, (int)strlen(msg), msg);
    send(sock, response, len, 0);
}

static void handle_upload(HttpServer* srv, int sock, const char* headers) {
    char content_type[256] = {0};
    char content_length_str[32] = {0};

    if (!get_header(headers, "Content-Type", content_type, sizeof(content_type)) ||
        !get_header(headers, "Content-Length", content_length_str, sizeof(content_length_str))) {
        serve_error(sock, 400, "Bad Request");
        return;
    }

    int content_length = atoi(content_length_str);
    if (content_length <= 0 || content_length > 50 * 1024 * 1024) { // 50MB limit
        serve_error(sock, 413, "File too large");
        return;
    }

    char boundary[256] = {0};
    if (!get_boundary(content_type, boundary, sizeof(boundary))) {
        serve_error(sock, 400, "No boundary");
        return;
    }

    // Read the body in chunks, stream to file
    char* recv_buf = malloc(RECV_BUF_SIZE);
    if (!recv_buf) {
        serve_error(sock, 500, "Out of memory");
        return;
    }

    strcpy(srv->status_msg, "Receiving file...");

    // Read until we find the part headers (end with \r\n\r\n)
    int body_read = 0;
    int buf_used = 0;
    bool found_data_start = false;
    int data_start_offset = 0;

    // First, read enough to get past the multipart part headers
    while (body_read < content_length && buf_used < RECV_BUF_SIZE) {
        int to_read = RECV_BUF_SIZE - buf_used;
        if (to_read > content_length - body_read)
            to_read = content_length - body_read;

        int n = recv(sock, recv_buf + buf_used, to_read, 0);
        if (n <= 0) break;
        buf_used += n;
        body_read += n;

        // Look for \r\n\r\n which ends the part headers
        if (!found_data_start) {
            for (int i = 3; i < buf_used; i++) {
                if (recv_buf[i-3] == '\r' && recv_buf[i-2] == '\n' &&
                    recv_buf[i-1] == '\r' && recv_buf[i]   == '\n') {
                    found_data_start = true;
                    data_start_offset = i + 1;
                    break;
                }
            }
        }

        if (found_data_start) break;
    }

    if (!found_data_start) {
        free(recv_buf);
        serve_error(sock, 400, "Malformed upload");
        strcpy(srv->status_msg, "Upload failed (bad format)");
        return;
    }

    // Extract filename from part headers
    recv_buf[data_start_offset - 2] = '\0'; // null-terminate part headers
    char filename[MAX_FILENAME_LEN] = {0};
    if (!get_filename(recv_buf, filename, sizeof(filename))) {
        strncpy(filename, "uploaded.epub", sizeof(filename) - 1);
    }

    // Open output file
    char filepath[MAX_PATH_LEN];
    snprintf(filepath, sizeof(filepath), "%s/%s", BOOKS_DIR, filename);

    FILE* outfile = fopen(filepath, "wb");
    if (!outfile) {
        free(recv_buf);
        serve_error(sock, 500, "Cannot write file");
        strcpy(srv->status_msg, "Upload failed (write error)");
        return;
    }

    // Write the data we already have (minus the part headers)
    int initial_data = buf_used - data_start_offset;
    if (initial_data > 0) {
        fwrite(recv_buf + data_start_offset, 1, initial_data, outfile);
    }

    // Continue reading the rest of the body
    while (body_read < content_length) {
        int to_read = RECV_BUF_SIZE;
        if (to_read > content_length - body_read)
            to_read = content_length - body_read;

        int n = recv(sock, recv_buf, to_read, 0);
        if (n <= 0) break;
        fwrite(recv_buf, 1, n, outfile);
        body_read += n;

        // Update status
        int pct = (int)((float)body_read / content_length * 100);
        snprintf(srv->status_msg, sizeof(srv->status_msg),
                 "Receiving... %d%%", pct);
    }

    fclose(outfile);

    // The file now has trailing boundary data. Trim it.
    // The end boundary is: \r\n--<boundary>--\r\n
    int boundary_suffix_len = 2 + 2 + strlen(boundary) + 2 + 2; // \r\n--boundary--\r\n

    // Reopen and truncate
    FILE* f = fopen(filepath, "r+b");
    if (f) {
        fseek(f, 0, SEEK_END);
        long file_size = ftell(f);
        if (file_size > boundary_suffix_len) {
            // Read the last bytes to verify boundary
            long new_size = file_size - boundary_suffix_len;
            // Use ftruncate equivalent: rewrite file
            fclose(f);

            // Simple approach: truncate by rewriting
            f = fopen(filepath, "r+b");
            if (f) {
                fseek(f, new_size, SEEK_SET);
                // Check if we see \r\n-- at this position
                char check[4];
                fread(check, 1, 4, f);
                if (check[0] == '\r' && check[1] == '\n' && check[2] == '-' && check[3] == '-') {
                    // Good, truncate here
                    fclose(f);
                    truncate(filepath, new_size);
                } else {
                    fclose(f);
                    // Try different offset (some browsers add extra \r\n)
                    truncate(filepath, file_size - boundary_suffix_len - 2);
                }
            }
        } else {
            fclose(f);
        }
    }

    free(recv_buf);

    // Send success response
    serve_page(sock, upload_ok_html);

    snprintf(srv->status_msg, sizeof(srv->status_msg),
             "Uploaded: %s", filename);
    srv->upload_complete = true;
}

static void handle_connection(HttpServer* srv, int sock) {
    char headers[4096];
    int hlen = read_headers(sock, headers, sizeof(headers));
    if (hlen <= 0) {
        close(sock);
        return;
    }

    if (strncmp(headers, "GET", 3) == 0) {
        serve_page(sock, upload_html);
    } else if (strncmp(headers, "POST", 4) == 0) {
        handle_upload(srv, sock, headers);
    } else {
        serve_error(sock, 405, "Method Not Allowed");
    }

    close(sock);
}

void httpd_poll(HttpServer* srv) {
    if (!srv->running || srv->server_sock < 0)
        return;

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client = accept(srv->server_sock, (struct sockaddr*)&client_addr, &addr_len);

    if (client < 0)
        return; // No connection (non-blocking)

    // Set client socket to blocking mode for reliable I/O
    int flags = fcntl(client, F_GETFL, 0);
    fcntl(client, F_SETFL, flags & ~O_NONBLOCK);

    handle_connection(srv, client);
}

void httpd_shutdown(HttpServer* srv) {
    if (srv->server_sock >= 0) {
        close(srv->server_sock);
        srv->server_sock = -1;
    }
    srv->running = false;

    if (soc_initialized) {
        socExit();
        if (soc_buffer) {
            free(soc_buffer);
            soc_buffer = NULL;
        }
        soc_initialized = false;
    }
}
