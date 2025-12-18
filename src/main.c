#include <rp6502.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#define COMMAND_TIMEOUT 10000  // milliseconds
#define RESPONSE_BUFFER_SIZE 256
#define WIFI_SSID "Cudy24G"
#define WIFI_PASSWORD "ZAnne19991214"
#define SERVER_IP "192.168.10.174"
//#define SERVER_IP "test.mosquitto.org"
#define SERVER_PORT "1883"

static int g_tcp_fd = -1;

char* my_strstr(const char *haystack, const char *needle)
{
    const char *h, *n;
    
    if (!*needle)
        return (char*)haystack;
    
    while (*haystack)
    {
        h = haystack;
        n = needle;
        
        while (*h && *n && (*h == *n))
        {
            h++;
            n++;
        }
        
        if (!*n)
            return (char*)haystack;
        
        haystack++;
    }
    
    return NULL;
}

bool check_response(char *response, const char *expected[], int num_expected)
{
    int i;

    /* Treat any ERROR as failure even if OK also appears */
    if (my_strstr(response, "ERROR") != NULL)
        return false;

    for (i = 0; i < num_expected; i++)
    {
        if (my_strstr(response, expected[i]) != NULL)
        {
            return true;
        }
    }
    return false;
}

void my_sprintf(char *dest, const char *fmt, const char *s1, const char *s2)
{
    // Simple sprintf for our specific format strings
    while (*fmt)
    {
        if (*fmt == '%' && *(fmt + 1) == 's')
        {
            // Copy first string
            const char *src = s1;
            while (*src)
                *dest++ = *src++;
            s1 = s2;  // Next %s will use s2
            fmt += 2;
        }
        else
        {
            *dest++ = *fmt++;
        }
    }
    *dest = '\0';
}

void print(char *s)
{
    while (*s)
        if (RIA.ready & RIA_READY_TX_BIT)
            RIA.tx = *s++;
}

/* Helper function to copy string to XRAM */
void xram_strcpy_old(unsigned int addr, const char* str) {
    int i;
    RIA.addr0 = addr;
    for (i = 0; str[i]; i++) {
        RIA.rw0 = str[i];
    }
    RIA.rw0 = 0;
}

void xram_strcpy(unsigned int addr, const char* str) {
    int i;
    RIA.step0 = 1;  // Enable auto-increment
    RIA.addr0 = addr;
    for (i = 0; str[i]; i++) {
        RIA.rw0 = str[i];
    }
    RIA.rw0 = 0;
}

void xram_strcpy_old1(unsigned int addr, const char* str) {
    int i;
    for (i = 0; str[i]; i++) {
        RIA.addr0 = addr + i;
        RIA.rw0 = str[i];
    }
    RIA.addr0 = addr + i;
    RIA.rw0 = 0;
}

void delay_ms(int ms)
{
    int i, j;
    /* Time tracking is now done via loop counter */
    for (i = 0; i < ms; i++)
    {
        for (j = 0; j < 100; j++)
            ;
    }
}

// Send a string to the modem
void send_to_modem(int fd, char *cmd)
{
    char *p = cmd;
    while (*p)
    {
        ria_push_char(*p++);
        ria_set_ax(fd);
        while (!ria_call_int(RIA_OP_WRITE_XSTACK))
            ;
    }
    // Send CR+LF for AT commands
    ria_push_char('\r');
    ria_set_ax(fd);
    while (!ria_call_int(RIA_OP_WRITE_XSTACK))
        ;
    ria_push_char('\n');
    ria_set_ax(fd);
    while (!ria_call_int(RIA_OP_WRITE_XSTACK))
        ;
}

bool read_modem_response(int fd, char *buffer, int max_len, int timeout_ms)
{
    int idx = 0;
    int elapsed = 0;
    int idle_count = 0;
    char ch;
    
    buffer[0] = '\0';
    
    // Wait a bit for response to start
    delay_ms(100);
    
    while (elapsed < timeout_ms && idx < max_len - 1)
    {
        ria_push_char(1);
        ria_set_ax(fd);
        if (ria_call_int(RIA_OP_READ_XSTACK))
        {
            ch = ria_pop_char();
            buffer[idx++] = ch;
            buffer[idx] = '\0';
            idle_count = 0;
            elapsed = 0;
        }
        else
        {
            idle_count++;
            // If we've received data and then no data for a while, we're done
            if (idx > 0 && idle_count > 50)
                break;
            delay_ms(10);
            elapsed += 10;
        }
    }
    
    return idx > 0;
}

bool send_at_command(int fd, char *cmd, const char *expected[], int num_expected)
{
    static char response[RESPONSE_BUFFER_SIZE];
    
    print("Sending: ");
    print(cmd);
    print("\r\n");
    
    send_to_modem(fd, cmd);
    
    // Wait for response
    if (read_modem_response(fd, response, RESPONSE_BUFFER_SIZE, COMMAND_TIMEOUT))
    {
        print("Response: ");
        print(response);
        print("\r\n");
        
        /* Special-case bare AT: accept OK even if noise ERROR appears */
        if ((cmd[0] == 'A' && cmd[1] == 'T' && cmd[2] == '\0') && my_strstr(response, "OK"))
        {
            print("OK\r\n");
            return true;
        }

        if (check_response(response, expected, num_expected))
        {
            print("OK\r\n");
            return true;
        }
        else
        {
            print("Unexpected response\r\n");
            return false;
        }
    }
    else
    {
        print("Timeout\r\n");
        return false;
    }
}

bool send_at_command_long(int fd, char *cmd, const char *expected[], int num_expected)
{
    static char response[RESPONSE_BUFFER_SIZE];
    
    print("Sending: ");
    print(cmd);
    print("\r\n");
    
    send_to_modem(fd, cmd);
    
    // Wait for response with longer timeout (20 seconds for WiFi)
    if (read_modem_response(fd, response, RESPONSE_BUFFER_SIZE, 20000))
    {
        print("Response: ");
        print(response);
        print("\r\n");
        
        if (check_response(response, expected, num_expected))
        {
            print("Connected!\r\n");
            return true;
        }
        else
        {
            print("Connection failed\r\n");
            return false;
        }
    }
    else
    {
        print("Connection timeout\r\n");
        return false;
    }
}

bool init_wifi(int fd)
{
    static char cmd[128];
    static const char *ok_resp[] = {"OK"};
    static const char *connect_resp[] = {"OK", "WIFI CONNECTED", "WIFI GOT IP"};
    static const char *tcp_resp[] = {"CONNECT", "ALREADY CONNECTED"};
    
    print("Initializing WiFi...\r\n");
    
    // AT - Test command
    if (!send_at_command(fd, "AT", ok_resp, 1))
        return false;

    delay_ms(500);
    
    // ATE0 - Disable echo
    if (!send_at_command(fd, "ATE0", ok_resp, 1))
        return false;
    delay_ms(1000);  // Wait for echo disable to take effect
    
    // AT+CWJAP - Join access point (can take 10-15 seconds)
    print("Connecting to WiFi (may take 15+ seconds)...\r\n");
    my_sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASSWORD);

    if (!send_at_command_long(fd, cmd, connect_resp, 3))
        return false;

    delay_ms(2000);  // Wait after connection
    
    // AT+CIFSR - Get IP address
//    if (!send_at_command(fd, "AT+CIFSR", ok_resp, 1))
  //      return false;
    
    // AT+CIPMUX=0 - Single connection mode
  //  if (!send_at_command(fd, "AT+CIPMUX=0", ok_resp, 1))
    //    return false;
    
    // AT+CIPRECVMODE=0 - Active receive mode (data pushed via +IPD)
    // This may fail on some firmware - continue even if it errors
//    send_at_command(fd, "AT+CIPRECVMODE=0", ok_resp, 1);
    
    // AT+CIPSTART - Start TCP connection
    my_sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%s", SERVER_IP, SERVER_PORT);

    if (!send_at_command(fd, cmd, tcp_resp, 3))
        return false;

    delay_ms(2000);  // Wait for connection to establish and stabilize
    
    // In normal mode, we use AT+CIPSEND for reliable message delivery
    print("Normal mode active. Ready to send/receive with AT+CIPSEND.\\r\\n");
    
    print("WiFi initialized successfully!\r\n");
    return true;
}

int main() {
    char broker[] = SERVER_IP;
    char client_id[] = "rp6502_demo";
    unsigned int port = 1883;
    char sub_topic[] = "rp6502/demo/#";
    unsigned int sub_len;
    char topic1[] = "rp6502/demo/temperature";
    char payload1[] = "22.5 C";
    char topic2[] = "rp6502/demo/humidity";
    char payload2[] = "65%";
    char status_topic[] = "rp6502/demo/status";
    char status_payload[] = "online";
    int msg_count = 0;
    int i, j;
    volatile long k;
    unsigned int msg_len, topic_len, bytes_read;
    
    print("=== Complete MQTT Example ===\n\n");
    
    /* STEP 1: Connect to WiFi */
    print("[1/6] Connecting to WiFi...\n");
    g_tcp_fd = open("AT:", 0);    

    print("Waiting for WiFi connection...\n");

    if (!init_wifi(g_tcp_fd))
    {
        print("WiFi initialization failed.\r\n");
        return -1;
    }    

    print("WiFi connected!\n");
    
    /* STEP 2: Connect to MQTT Broker */
    print("[2/6] Connecting to MQTT broker...\n");
    
    // print("Wrote string, checking: ");
    // RIA.step0 = 1;
    // RIA.addr0 = 0x0000;
    // for (i = 0; i < 20; i++) {
    //     char c = RIA.rw0;
    //     if (c == 0) break;
    //     putchar(c);
    // }
    // print("\n");    

    xram_strcpy(0x0000, broker);
    xram_strcpy(0x0100, client_id);

    printf("Broker: %s:%d\n", broker, port);
    printf("Client: %s\n", client_id);
    
    // Initiate connection
    RIA.xstack = port >> 8;      // port high
    RIA.xstack = port & 0xFF;    // port low

    // Push client_id last (will be popped first with api_pop_uint16)
    RIA.xstack = 0x0100 >> 8;    // client_id high
    RIA.xstack = 0x0100 & 0xFF;  // client_id low

    // Put hostname address in A/X
    RIA.a = 0x0000 & 0xFF;       // low
    RIA.x = 0x0000 >> 8;    

    RIA.op = 0x30;  // mq_connect
    
    if (RIA.a != 0) {
        printf("ERROR: Connection failed: %d\n", RIA.a);
        return 1;
    }
    
    /* Wait for connection */
    print("Waiting for MQTT connection...");

    for (i = 0; i < 50; i++) {
        for (k = 0; k < 10000; k++);
        RIA.op = 0x38;  /* mq_connected */
        if (RIA.a == 1) {
            print(" CONNECTED!\n\n");
            break;
        }
        if (i % 5 == 0) print(".");
    }
    
    RIA.op = 0x38;  /* Check connection status */

    if (RIA.a != 1) {
        print("\nERROR: Connection timeout\n");
        return 1;
    }
    
    /* STEP 3: Subscribe to Topics */
    print("[3/6] Subscribing to topics...\n");
    
    xram_strcpy(0x0200, sub_topic);
    sub_len = strlen(sub_topic);
    
    printf("Subscribing to: %s\n", sub_topic);
    
    RIA.xstack = 0;                 // QoS 0
    RIA.xstack = sub_len & 0xFF;
    RIA.xstack = sub_len >> 8;
    RIA.a = 0x00; RIA.x = 0x02;
    RIA.op = 0x33;  // mq_subscribe
    
    if (RIA.a == 0) {
        print("Subscribed successfully!\n\n");
    } else {
        print("ERROR: Subscribe failed\n");
        return 1;
    }
    
    /* STEP 4: Publish Messages */
    print("[4/6] Publishing messages...\n");
    
    /* Publish message 1 */
    xram_strcpy(0x0300, topic1);
    xram_strcpy(0x0400, payload1);
    
    printf("Publishing: %s -> %s\n", topic1, payload1);
    
    RIA.xstack = 0;                              /* QoS 0 */
    RIA.xstack = 0;                              /* retain = false */
    RIA.xstack = strlen(topic1) & 0xFF;
    RIA.xstack = strlen(topic1) >> 8;
    RIA.xstack = 0x00; RIA.xstack = 0x03;        /* topic addr */
    RIA.xstack = strlen(payload1) & 0xFF;
    RIA.xstack = strlen(payload1) >> 8;
    RIA.a = 0x00; RIA.x = 0x04;                  /* payload addr */
    RIA.op = 0x32;  /* mq_publish */
    
    if (RIA.a != 0) {
        print("ERROR: Publish 1 failed\n");
    } else {
        print("Message 1 published!\n");
    }
    
    /* Small delay */
    for (k = 0; k < 50000L; k++);
    
    /* Publish message 2 */
    xram_strcpy(0x0300, topic2);
    xram_strcpy(0x0400, payload2);
    
    printf("Publishing: %s -> %s\n", topic2, payload2);
    
    RIA.xstack = 0; RIA.xstack = 0;
    RIA.xstack = strlen(topic2) & 0xFF;
    RIA.xstack = strlen(topic2) >> 8;
    RIA.xstack = 0x00; RIA.xstack = 0x03;
    RIA.xstack = strlen(payload2) & 0xFF;
    RIA.xstack = strlen(payload2) >> 8;
    RIA.a = 0x00; RIA.x = 0x04;
    RIA.op = 0x32;  // mq_publish
    
    if (RIA.a == 0) {
        print("Message 2 published!\n");
    }
    
    /* Publish status with retain flag */
    xram_strcpy(0x0300, status_topic);
    xram_strcpy(0x0400, status_payload);
    
    //print("Publishing: %s -> %s (retained)\n", status_topic, status_payload);
    
    RIA.xstack = 0;                              /* QoS 0 */
    RIA.xstack = 1;                              /* retain = TRUE */
    RIA.xstack = strlen(status_topic) & 0xFF;
    RIA.xstack = strlen(status_topic) >> 8;
    RIA.xstack = 0x00; RIA.xstack = 0x03;
    RIA.xstack = strlen(status_payload) & 0xFF;
    RIA.xstack = strlen(status_payload) >> 8;
    RIA.a = 0x00; RIA.x = 0x04;
    RIA.op = 0x32;  /* mq_publish */
    
    if (RIA.a == 0) {
        print("Status published and retained!\n\n");
    }
    
    /* STEP 5: Listen for Messages */
    print("[5/6] Listening for incoming messages (20 seconds)...\n");
    print("Note: We'll receive our own published messages\n\n");
    
    for (i = 0; i < 200; i++) {  /* 20 seconds */
        /* Poll for messages */
        RIA.op = 0x35;  /* mq_poll */
        msg_len = RIA.a | (RIA.x << 8);
        
        if (msg_len > 0) {
            msg_count++;
            printf("\n=== Message %d (Payload: %d bytes) ===\n", msg_count, msg_len);
            
            /* Get topic */
            RIA.xstack = 128 & 0xFF;
            RIA.xstack = 128 >> 8;
            RIA.a = 0x00; RIA.x = 0x05;
            RIA.op = 0x37;  /* mq_get_topic */
            
            topic_len = RIA.a | (RIA.x << 8);
            
            printf("Topic: ");
            RIA.addr0 = 0x0500;
            for (j = 0; j < topic_len; j++) {
                putchar(RIA.rw0);
            }
            printf("\n");
            
            /* Read message */
            RIA.xstack = 255 & 0xFF;
            RIA.xstack = 255 >> 8;
            RIA.a = 0x00; RIA.x = 0x06;
            RIA.op = 0x36;  /* mq_read_message */
            
            bytes_read = RIA.a | (RIA.x << 8);
            
            printf("Payload: ");
            RIA.addr0 = 0x0600;
            for (j = 0; j < bytes_read; j++) {
                putchar(RIA.rw0);
            }
            printf("\n");
        }
        
        /* Progress indicator */
        if (i % 20 == 0 && i > 0) {
            printf(".");
            fflush(stdout);
        }
        
        /* Delay ~100ms */
        for (k = 0; k < 10000; k++);
    }
    
    printf("\n\nReceived %d message%s total\n\n", 
           msg_count, msg_count == 1 ? "" : "s");
    
    /* STEP 6: Disconnect */
    printf("[6/6] Disconnecting from broker...\n");
    RIA.op = 0x31;  /* mq_disconnect */
    
    if (RIA.a == 0) {
        printf("Disconnected successfully!\n");
    }
    
    printf("\n=== EXAMPLE COMPLETE ===\n");
    printf("Summary:\n");
    printf("  - Connected to %s\n", broker);
    printf("  - Subscribed to: %s\n", sub_topic);
    printf("  - Published 3 messages\n");
    printf("  - Received %d messages\n", msg_count);
    printf("  - Disconnected cleanly\n");
    
    return 0;
}
