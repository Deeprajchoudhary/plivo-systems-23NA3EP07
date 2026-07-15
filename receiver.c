/* BASELINE RECEIVER (C) — naive on purpose. Rewrite it (C, C++, Go, or Rust).
 *
 * Ports (all 127.0.0.1):
 *   bind 47002  <- media from your sender, via the hostile relay
 *   send 47020  -> harness player. MUST be: 4-byte big-endian seq +
 *                  160-byte payload. Frame i counts only if it arrives
 *                  BEFORE its deadline t0 + DELAY_MS + i*20ms.
 *   send 47003  -> feedback to your sender, via the relay (optional)
 *
 * This baseline forwards whatever arrives straight to the player: lost
 * frames stay lost, late frames stay late, duplicates are re-sent
 * harmlessly. All yours to fix — jitter buffer, reordering, recovery.
 *
 * Env vars available: T0, DURATION_S, DELAY_MS. Harness kills the process
 * at run end; a forever-loop is fine.
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

bool played[65536] = {false};
bool has_fec_buf[65536] = {false};
unsigned char payload_buf[65536][160];
unsigned char fec_buf[65536][160];

uint32_t highest_seq = 0;
bool highest_seq_init = false;

int out_fd;
struct sockaddr_in player;

void play_frame(uint32_t seq, unsigned char *payload) {
    uint16_t idx = seq%65536;
     if(played[idx])  return;
    
    unsigned char out_buf[164];
     uint32_t net_seq = htonl(seq);
    memcpy(out_buf, &net_seq, 4);
    memcpy(out_buf + 4, payload, 160);
    
     sendto(out_fd, out_buf, 164, 0, (struct sockaddr *)&player, sizeof player);
    
    played[idx] = true;
     memcpy(payload_buf[idx], payload, 160);
}

int main(void) {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr) < 0) {
        perror("bind 47002");
        return 1;
    }

    out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&player, 0, sizeof player);
    
    player.sin_family = AF_INET;
    player.sin_port = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");

    unsigned char buf[2048];
    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
        if (n < 162) continue;
        uint8_t seq8 = buf[0];
        uint8_t has_fec = buf[1];

        uint32_t seq;
        if (!highest_seq_init) {
            seq = seq8;
            highest_seq = seq;
            highest_seq_init = true;
        } else {
            uint32_t diff = (seq8 - (highest_seq & 0xFF)) & 0xFF;
            if (diff < 128) {
                seq = highest_seq + diff;
            } else {
                seq = highest_seq - (256 - diff);
            }
            if (seq > highest_seq) highest_seq = seq;
        }

          uint16_t clear_idx = (seq + 1000) % 65536;
          played[clear_idx] = false;
          has_fec_buf[clear_idx] = false;

           uint16_t idx = seq % 65536;
          play_frame(seq, buf + 2);

         if (has_fec && n >= 322) {
            has_fec_buf[idx] = true;
            memcpy(fec_buf[idx], buf + 162, 160);
        }

        uint32_t start = (highest_seq > 20) ? highest_seq - 20 : 0;
        for (uint32_t s = start; s <= highest_seq; s++) {
            if (s < 2) continue;

            uint16_t s_idx = s % 65536;
            if (has_fec_buf[s_idx]) {
                uint16_t prev1 = (s - 1) % 65536;
                uint16_t prev2 = (s - 2) % 65536;

                if (!played[prev1] && played[prev2]) {
                    unsigned char recovered[160];
                    for (int k = 0; k < 160; k++) {
                        recovered[k] = fec_buf[s_idx][k] ^ payload_buf[prev2][k];
                    }
                    play_frame(s - 1, recovered);
                }
                else if (!played[prev2] && played[prev1]) {
                    unsigned char recovered[160];
                    for (int k = 0; k < 160; k++) {
                        recovered[k] = fec_buf[s_idx][k] ^ payload_buf[prev1][k];
                    }
                    play_frame(s - 2, recovered);
                }
            }
        }
    }
       
    return 0;
}
