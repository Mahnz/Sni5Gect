#include "shadower/hdr/constants.h"
#include "shadower/hdr/source.h"
#include "shadower/hdr/utils.h"
double   sample_rate = 23.04e6;
double   center_freq = 3427.5e6;
uint32_t rx_gain     = 60;
uint32_t tx_gain     = 51;

int main()
{
  /* initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::debug);

  create_source_t limesdr_source = load_source(limesdr_source_module_path);

  ShadowerConfig config = {};
  config.dl_freq        = center_freq;
  config.ul_freq        = center_freq;
  config.rx_gain        = rx_gain;
  config.tx_gain        = tx_gain;
  config.sample_rate    = sample_rate;
  config.source_params =
      "logLevel:5,port0:\"dev0\",dev0:\"XTRX\",dev0_chipIndex:0,"
      "dev0_linkFormat:\"I12\",dev0_rx_path:\"LNAH\",dev0_tx_path:\"Band1\","
      "dev0_max_channels_to_use:1,dev0_calibration:\"none\",dev0_rx_gfir_enable:0,dev0_tx_gfir_enable:0";
  Source* source = limesdr_source(config);

  uint32_t           sf_len         = sample_rate * SF_DURATION;
  cf_t*              buffer         = srsran_vec_cf_malloc(sf_len);
  srsran_timestamp_t ts             = {};
  uint32_t           subframe_count = 0;

  std::string   output_file = "/root/records/output.fc32";
  std::ofstream out(output_file, std::ios::binary);
  if (!out.is_open()) {
    std::cout << "Failed to open output file" << std::endl;
    return -1;
  }

  while (subframe_count < 20000) {
    int samplesRead = source->receive(buffer, sf_len, &ts);
    if (samplesRead == 0) {
      continue;
    }
    if (subframe_count++ % 100 == 0) {
      printf(".");
      fflush(stdout);
    }
    out.write(reinterpret_cast<char*>(buffer), samplesRead * sizeof(cf_t));
  }
  source->close();
}