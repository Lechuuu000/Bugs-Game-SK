#ifndef SIK_ROBAKI_UTILS_H
#define SIK_ROBAKI_UTILS_H

#include <ctime>
#include <sys/time.h>
#include <cstdint>
#include <arpa/inet.h>
#include <endian.h>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <getopt.h>
#include <string>
#include <iostream>

using Time = struct timeval;

uint32_t elapsed_time_us(Time& prev_time) {
    Time curr_time;
    gettimeofday(&prev_time, nullptr);

    int64_t diff = (curr_time.tv_usec - prev_time.tv_usec);
    if (diff < 0) {
        diff = -diff;
        diff -= 1000000;
    }
    diff += (curr_time.tv_sec - prev_time.tv_sec) * 1000000;
    return (uint32_t) diff;
}

uint32_t elapsed_time_ms(Time& prev_time) {
    return elapsed_time_us(prev_time)/1000;
}

void update_timestamp(Time& timestamp) {
    gettimeofday(&timestamp, nullptr);
}

static uint32_t crc32_table[256];

static void build_crc32_table(void) {
    for(uint32_t i=0;i<256;i++) {
        uint32_t ch=i;
        uint32_t crc=0;
        for(size_t j=0;j<8;j++) {
            uint32_t b=(ch^crc)&1;
            crc>>=1;
            if(b) crc=crc^0xEDB88320;
            ch>>=1;
        }
        crc32_table[i]=crc;
    }
}

uint32_t calculate_crc32(const char *s,size_t n) {
    static bool initialized = false;
    if(!initialized) {
        build_crc32_table();
        initialized = true;
    }

    uint32_t crc=0xFFFFFFFF;
    for(size_t i=0;i<n;i++) {
        char ch=s[i];
        uint32_t t=(ch^crc)&0xFF;
        crc=(crc>>8)^crc32_table[t];
    }
    return ~crc;
}

/// Funkcja wzięta z labów z sieci komputerowych
void syserr(const char *fmt, ...) {
    va_list fmt_args;
    int err;

    fprintf(stderr, "ERROR: ");
    err = errno;

    va_start(fmt_args, fmt);
    if (vfprintf(stderr, fmt, fmt_args) < 0) {
        fprintf(stderr, " (also error in syserr) ");
    }
    va_end(fmt_args);
    fprintf(stderr, " (%d; %s)\n", err, strerror(err));
    exit(EXIT_FAILURE);
}

struct CliOptions {
    uint16_t port = 2021;
    int seed;
    short turning_speed = 6;
    short rounds_per_sec = 50;
    short width = 640;
    short height = 480;

    CliOptions() { seed = time(nullptr); }
};

CliOptions get_options(int argc, char **argv) {
    CliOptions options;
    while(true) {
        switch (getopt(argc, argv, "p:ns:nt:nv:nw:nh:n")) {
            case 'p':
                options.port = std::stoi(optarg);
                break;
            case 's':
                options.seed = std::stoi(optarg);
                break;
            case 't':
                options.turning_speed = std::stoi(optarg);
                break;
            case 'v':
                options.rounds_per_sec = std::stoi(optarg);
                break;
            case 'w':
                options.width = std::stoi(optarg);
                break;
            case 'h':
                options.height = std::stoi(optarg);
                break;
            case -1:
                return options;
            default:
                goto error;
        }
    }
    error:
    std::cout << "Usage: ./screen-worms-server [-p n] [-s n] [-t n] [-v n] [-w n] [-h n]\n";
    exit(1);
}


#endif //SIK_ROBAKI_UTILS_H
