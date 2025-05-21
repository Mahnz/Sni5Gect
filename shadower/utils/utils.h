#pragma once
#include "shadower/utils/arg_parser.h"
#include "srsran/asn1/rrc_nr.h"
#include "srsran/config.h"
extern "C" {
#include "srsran/phy/phch/prach.h"
}
#include "srsran/srslog/srslog.h"
#include <inttypes.h>
#include <string>
#include <vector>

/* Write the IQ samples to a file so that we can use tools like matlab or spectrogram-py to debug */
void write_record_to_file(cf_t* buffer, uint32_t length, char* name = nullptr, const std::string& folder = "records");

/* Load the IQ samples from a file */
bool load_samples(const std::string& filename, cf_t* buffer, size_t nsamples);

/* Read binary form configuration dumped structure */
bool read_raw_config(const std::string& filename, uint8_t* buffer, size_t size);

/* Initialize logger */
srslog::basic_logger& srslog_init(ShadowerConfig* config);

/* Calculate the RA-rnti from SIB1 configuration */
std::vector<uint16_t> get_ra_rnti_list(asn1::rrc_nr::sib1_s& sib1, ShadowerConfig& config);