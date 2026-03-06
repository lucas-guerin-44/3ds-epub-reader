#ifndef HTTPD_H
#define HTTPD_H

#include <stdbool.h>

#define HTTPD_PORT 8080

typedef struct {
    int   server_sock;
    bool  running;
    char  ip_str[32];
    int   port;
    char  status_msg[128];
    bool  upload_complete;  // set after successful upload
} HttpServer;

// Initialize the HTTP server (create socket, bind, listen)
bool httpd_init(HttpServer* srv, int port);

// Poll for connections (non-blocking accept, then blocking handle)
// Call this each frame while on the transfer screen
void httpd_poll(HttpServer* srv);

// Shut down the server
void httpd_shutdown(HttpServer* srv);

#endif
