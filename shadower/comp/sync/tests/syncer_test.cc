#include "shadower/comp/sync/syncer.h"
#include "shadower/source/source.h"
#include "shadower/utils/utils.h"
#include <getopt.h>

ShadowerConfig config = {};
Source*        source = nullptr;

bool on_cell_found(srsran_mib_nr_t& mib, uint32_t ncellid_)
{
  std::array<char, 512> mib_info_str = {};
  srsran_pbch_msg_nr_mib_info(&mib, mib_info_str.data(), (uint32_t)mib_info_str.size());
  printf("Found cell: %s %u\n", mib_info_str.data(), ncellid_);
  return true;
}

void syncer_exit_handler()
{
  exit(0);
}

// Handler for syncer to push new task to the task queue
void push_new_task(std::shared_ptr<Task>& task) {}

void parse_args(int argc, char* argv[])
{
  int opt;

  while ((opt = getopt(argc, argv, "sfbBtdcCrp")) != -1) {
    switch (opt) {
      case 's': {
        double srateMHz    = atof(argv[optind]);
        config.sample_rate = srateMHz * 1e6;
        printf("Using sample rate: %f MHz\n", srateMHz);
        break;
      }
      case 'f': {
        double freqMHz = atof(argv[optind]);
        config.dl_freq = freqMHz * 1e6;
        config.ul_freq = config.dl_freq;
        printf("Using center frequency: %f MHz\n", config.dl_freq);
        break;
      }
      case 'b': {
        double ssbFreqMHz = atof(argv[optind]);
        config.ssb_freq   = ssbFreqMHz * 1e6;
        printf("Using SSB Frequency: %f MHz\n", config.ssb_freq);
        break;
      }
      case 'B':
        config.band = atoi(argv[optind]);
        printf("Using band: %u\n", config.band);
        break;
      case 't':
        config.source_type = argv[optind];
        printf("Using source type: %s\n", config.source_type.c_str());
        break;
      case 'd':
        config.source_params = argv[optind];
        printf("Using device args: %s\n", config.source_params.c_str());
        break;
      case 'c':
        config.nof_channels = atoi(argv[optind]);
        printf("Using number of channels: %u\n", config.nof_channels);
        break;
      case 'C':
        config.scs_ssb = srsran_subcarrier_spacing_from_str(argv[optind]);
        printf("Using SCS: %s\n", argv[optind]);
        break;
      case 'p':
        config.ssb_period = atoi(argv[optind]);
        printf("Using SSB period: %u slots\n", config.ssb_period);
        break;
      case 'r':
        config.enable_recorder = true;
        printf("Enable recorder\n");
        break;
      default:
        fprintf(stderr, "Unknown option or missing argument.\n");
        exit(EXIT_FAILURE);
    }
  }
  if (config.ssb_period == 0) {
    config.ssb_period = 20; // Default SSB period if not specified
  }
  config.rx_gain = 40;
  config.tx_gain = 0;
  srsran::srsran_band_helper helper;

  config.duplex_mode = helper.get_duplex_mode(config.band);
  config.ssb_pattern = helper.get_ssb_pattern(config.band, config.scs_ssb);
}

int main(int argc, char* argv[])
{
  config.band        = 78;
  config.ssb_pattern = srsran_ssb_pattern_t::SRSRAN_SSB_PATTERN_C;
  config.scs_ssb     = srsran_subcarrier_spacing_t::srsran_subcarrier_spacing_30kHz;
  config.rx_gain     = 60;
  parse_args(argc, argv);
  /* initialize logger */
  config.log_level             = srslog::basic_levels::debug;
  config.syncer_log_level      = srslog::basic_levels::debug;
  srslog::basic_logger& logger = srslog_init(&config);

  /* Initialize syncer args */
  syncer_args_t syncer_args = {
      .srate       = config.sample_rate,
      .scs         = config.scs_ssb,
      .dl_freq     = config.dl_freq,
      .ssb_freq    = config.ssb_freq,
      .pattern     = config.ssb_pattern,
      .duplex_mode = config.duplex_mode,
  };

  if (config.source_type == "uhd") {
    create_source_t uhd_source = load_source(uhd_source_module_path);
    source                     = uhd_source(config);
  } else if (config.source_type == "file") {
    create_source_t file_source = load_source(file_source_module_path);
    source                      = file_source(config);
  } else if (config.source_type == "limesdr") {
    create_source_t lime_source = load_source(file_source_module_path);
    source                      = lime_source(config);
  }
  /* Initialize syncer */
  Syncer* syncer = new Syncer(syncer_args, source, config);
  syncer->init();
  syncer->on_cell_found    = std::bind(on_cell_found, std::placeholders::_1, std::placeholders::_2);
  syncer->error_handler    = std::bind(syncer_exit_handler);
  syncer->publish_subframe = std::bind(push_new_task, std::placeholders::_1);
  syncer->start(0);
  syncer->wait_thread_finish();
  source->close();
}