#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <cstdint>
#include <string>
#define SF_DURATION 1e-3
#define NUM_SUBFRAME 10

// start + 16: ip.len 2 bytes
// start + 38: udp.length 2 bytes
// start + 49: mac-nr.direction
static constexpr uint8_t fake_pcap_header[] = {0x0,  0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
                                               0x8,  0x0, 0x45, 0x0,  0x0,  0xa9, 0x2d, 0x73, 0x40, 0x0,  0x40, 0x11,
                                               0x0,  0x0, 0x7f, 0x0,  0x0,  0x1,  0x7f, 0x0,  0x0,  0x1,  0xe7, 0xa6,
                                               0x27, 0xf, 0x0,  0x95, 0xfe, 0x9a, 0x6d, 0x61, 0x63, 0x2d, 0x6e, 0x72};

const uint32_t fake_pcap_header_len = 48;

enum direction_t { UL, DL };

#define RESET "\033[0m"    // Reset to default color
#define RED "\033[31m"     // Red
#define GREEN "\033[32m"   // Green
#define YELLOW "\033[33m"  // Yellow
#define BLUE "\033[34m"    // Blue
#define MAGENTA "\033[35m" // Magenta
#define CYAN "\033[36m"    // Cyan

static const double K  = 64.0;                      // TS 138.211 4.1
static const double Tc = 1.0 / (480000.0 * 4096.0); // TS 138.211 4.1

const std::string file_source_module_path    = "build/shadower/source/libfile_source.so";
const std::string uhd_source_module_path     = "build/shadower/source/libuhd_source.so";
const std::string limesdr_source_module_path = "build/shadower/source/liblimesdr_source.so";

static float TARGET_STOPBAND_SUPPRESSION = 60.0f;
#endif // CONSTANTS_H