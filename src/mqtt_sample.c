#include <rp6502.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

static int g_tcp_fd = -1;

/* Helper function to copy string to XRAM */
void xram_strcpy(unsigned int addr, const char* str) {
    int i;
    RIA.addr0 = addr;
    for (i = 0; str[i]; i++) {
        RIA.rw0 = str[i];
    }
    RIA.rw0 = 0;
}

/* Helper for modem AT commands */
void modem_cmd(const char* cmd) {
    int i;
    volatile long j;
    for (i = 0; cmd[i]; i++) putchar(cmd[i]);
    putchar('\r');
    for (j = 0; j < 50000; j++);  /* Wait for response */
}

int main() {
    char broker[] = "192.168.10.174";
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
    
    printf("=== Complete MQTT Example ===\n\n");
    
    /* STEP 1: Connect to WiFi */
    printf("[1/6] Connecting to WiFi...\n");
    g_tcp_fd = open("AT:", 0);    
    modem_cmd("AT");                                    /* Reset modem */
    modem_cmd("ATE0");                                    /* Reset modem */    
    //modem_cmd("AT+CWMODE=1");                           /* Station mode */
    modem_cmd("AT+CWJAP=\"Cudy24G\",\"ZAnne19991214\""); /* Connect to WiFi */
    printf("Waiting for WiFi connection...\n");
    for (k = 0; k < 2000000; k++);
    printf("WiFi connected!\n\n");
    
    /* STEP 2: Connect to MQTT Broker */
    printf("[2/6] Connecting to MQTT broker...\n");
    
    xram_strcpy(0x0000, broker);
    xram_strcpy(0x0100, client_id);
    
    printf("Broker: %s:%d\n", broker, port);
    printf("Client: %s\n", client_id);
    
    // Initiate connection
    RIA.xstack = port & 0xFF;
    RIA.xstack = port >> 8;
    RIA.xstack = 0x00; RIA.xstack = 0x00;  // hostname addr
    RIA.a = 0x00; RIA.x = 0x01;             // client_id addr
    RIA.op = 0x30;  // mq_connect
    
    if (RIA.a != 0) {
        printf("ERROR: Connection failed: %d\n", RIA.a);
        return 1;
    }
    
    /* Wait for connection */
    printf("Waiting for MQTT connection");
    for (i = 0; i < 50; i++) {
        for (k = 0; k < 10000; k++);
        RIA.op = 0x38;  /* mq_connected */
        if (RIA.a == 1) {
            printf(" CONNECTED!\n\n");
            break;
        }
        if (i % 5 == 0) printf(".");
    }
    
    RIA.op = 0x38;  /* Check connection status */
    if (RIA.a != 1) {
        printf("\nERROR: Connection timeout\n");
        return 1;
    }
    
    /* STEP 3: Subscribe to Topics */
    printf("[3/6] Subscribing to topics...\n");
    
    xram_strcpy(0x0200, sub_topic);
    sub_len = strlen(sub_topic);
    
    printf("Subscribing to: %s\n", sub_topic);
    
    RIA.xstack = 0;                 // QoS 0
    RIA.xstack = sub_len & 0xFF;
    RIA.xstack = sub_len >> 8;
    RIA.a = 0x00; RIA.x = 0x02;
    RIA.op = 0x33;  // mq_subscribe
    
    if (RIA.a == 0) {
        printf("Subscribed successfully!\n\n");
    } else {
        printf("ERROR: Subscribe failed\n");
        return 1;
    }
    
    /* STEP 4: Publish Messages */
    printf("[4/6] Publishing messages...\n");
    
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
        printf("ERROR: Publish 1 failed\n");
    } else {
        printf("Message 1 published!\n");
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
        printf("Message 2 published!\n");
    }
    
    /* Publish status with retain flag */
    xram_strcpy(0x0300, status_topic);
    xram_strcpy(0x0400, status_payload);
    
    printf("Publishing: %s -> %s (retained)\n", status_topic, status_payload);
    
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
        printf("Status published and retained!\n\n");
    }
    
    /* STEP 5: Listen for Messages */
    printf("[5/6] Listening for incoming messages (20 seconds)...\n");
    printf("Note: We'll receive our own published messages\n\n");
    
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
