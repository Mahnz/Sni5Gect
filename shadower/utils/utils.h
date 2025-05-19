#pragma once
#include "shadower/utils/arg_parser.h"
#include "srsran/config.h"
#include "srsran/phy/sync/ssb.h"
#include "srsran/srslog/srslog.h"
#include <inttypes.h>
#include <string>

/* Write the IQ samples to a file so that we can use tools like matlab or spectrogram-py to debug */
void write_record_to_file(cf_t* buffer, uint32_t length, char* name = nullptr, const std::string& folder = "records");

/* Load the IQ samples from a file */
bool load_samples(const std::string& filename, cf_t* buffer, size_t nsamples);

/* Initialize logger */
srslog::basic_logger& srslog_init(ShadowerConfig* config);

/* Helper function to initialize ssb */
bool init_ssb(srsran_ssb_t&               ssb,
              double                      srate,
              double                      dl_freq,
              double                      ssb_freq,
              srsran_subcarrier_spacing_t scs,
              srsran_ssb_pattern_t        pattern,
              srsran_duplex_mode_t        duplex_mode);