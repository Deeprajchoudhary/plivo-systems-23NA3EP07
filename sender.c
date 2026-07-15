/* BASELINE SENDER (C) — naive on purpose. Rewrite it (C, C++, Go, or Rust).
 *
 * Ports (all 127.0.0.1):
 *   bind 47010  <- harness source delivers frame i here at t0 + i*20ms
 *                  (format: 4-byte big-endian seq + 160-byte payload)
 *   send 47001  -> relay uplink toward the receiver (YOUR wire format)
 *   bind 47004  <- feedback from your receiver, via the relay (optional)
 *
 * This baseline forwards each frame once, unchanged, and ignores feedback.
 * No redundancy, no retransmission. It cannot pass. That is the point.
 *
 * Env vars available if you want them: T0 (epoch seconds, float),
 * DURATION_S, DELAY_MS. The harness kills this process when the run ends,
 * so a forever-loop is fine.
 *
 * build: make        run: python3 run.py --delay_ms 60
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47010);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr) < 0) {
        perror("bind 47010");
        return 1;
    }

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay = {0};
    relay.sin_family = AF_INET;
    relay.sin_port = htons(47001);
    relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    unsigned char buf[2048];

    unsigned char p1[160] = {0};
    unsigned char p2[160] = {0};
    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
        if(n < 164) continue;
        uint32_t seq;
        memcpy(&seq, buf, 4);
        seq = ntohl(seq);
        unsigned char out_buf[512];
        out_buf[0] = seq & 0xFF;
        uint8_t has_fec = (seq % 2 == 0) ? 1 : 0;
        out_buf[1] = has_fec;

        unsigned char *payload = buf + 4;
        memcpy(out_buf + 2, payload, 160);

        size_t out_len = 162;
        
        //Inject XOR parity of the last 2 frames
        if (has_fec) {
            for (int i = 0; i < 160; i++) {
                out_buf[162 + i] = p1[i] ^ p2[i];
            }
            out_len += 160;
        }
        

        
        /* your protocol design goes here; baseline = send once, as-is */
        sendto(out_fd, out_buf, out_len, 0, (struct sockaddr *)&relay, sizeof relay);

        memcpy(p2, p1, 160);
        memcpy(p1, payload, 160);
        
    }
    return 0;
}
