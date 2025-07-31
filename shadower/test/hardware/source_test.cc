#include "shadower/hdr/constants.h"
#include "shadower/hdr/source.h"
#include "shadower/hdr/utils.h"
double      sample_rate     = 23.04e6;
double      center_freq     = 3427.5e6;
std::string source_filename = "shadower/test/data/srsran-n78-20MHz/sib.fc32";
std::string sdr_args        = "type=b200";
uint32_t    rx_gain         = 40;
uint32_t    tx_gain         = 80;
float       send_delay      = 2e-3;

int main()
{
  /* initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::debug);

  /* Test the file source */
  Source*        source;
  uint32_t       sf_len          = sample_rate * SF_DURATION;
  uint32_t       slot_len        = sf_len / 2;
  ShadowerConfig config          = {};
  config.source_type             = "file";
  config.source_params           = source_filename;
  config.sample_rate             = sample_rate;
  create_source_t file_source    = load_source(file_source_module_path);
  source                         = file_source(config);
  cf_t*              test_buffer = srsran_vec_cf_malloc(sf_len);
  srsran_timestamp_t ts          = {};

  /* Test the SDR source */
  cf_t* tx_buffer = srsran_vec_cf_malloc(sf_len);
  if (!load_samples(source_filename, tx_buffer, sf_len)) {
    logger.error("Error loading samples\n");
    return -1;
  }
  char            filename[64];
  create_source_t uhd_source = load_source(uhd_source_module_path);
  config.source_params       = sdr_args;
  config.dl_freq             = center_freq;
  config.ul_freq             = center_freq;
  config.rx_gain             = rx_gain;
  config.tx_gain             = tx_gain;
  source                     = uhd_source(config);
  for (uint32_t i = 0; i < 50; i++) {
    sprintf(filename, "received_data_%u", i);
    /* Receive the samples again */
    source->receive(test_buffer, sf_len, &ts);
    srsran_timestamp_add(&ts, 0, send_delay);
    /* Send the samples out */
    source->send(tx_buffer + slot_len, slot_len, ts);
    write_record_to_file(test_buffer, sf_len, filename);
  }
  source->close();
}