#include "shadower/comp/workers/gnb_dl_worker.h"
#include "shadower/utils/arg_parser.h"
#include "shadower/utils/phy_cfg_utils.h"
#include "shadower/utils/safe_queue.h"
#include "shadower/utils/utils.h"
#include "srsran/support/srsran_test.h"
std::string    sample_file;
std::string    mib_config_file;
std::string    sib_config_file;
uint32_t       sib_config_size;
std::string    rrc_setup_config_file;
uint32_t       rrc_setup_config_size;
uint32_t       slot_idx = 0;
uint32_t       ncellid  = 1;
uint16_t       rnti     = 0xffff;
ShadowerConfig config   = {};

SafeQueue<std::vector<uint8_t> > dl_msg_queue;
SafeQueue<std::vector<uint8_t> > ul_msg_queue;

void parse_args(int argc, char* argv[])
{
  int opt;
  while ((opt = getopt(argc, argv, "sfbcRiImyYzZpMP")) != -1) {
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
      case 'c': {
        config.nof_channels = atoi(argv[optind]);
        printf("Using number of channels: %u\n", config.nof_channels);
        break;
      }
      case 'R': {
        rnti = atoi(argv[optind]);
        printf("Using RNTI: %u\n", rnti);
        break;
      }
      case 'I': {
        ncellid = atoi(argv[optind]);
        printf("Using cell id: %u\n", ncellid);
        break;
      }
      case 'm': {
        mib_config_file = argv[optind];
        printf("Using mib config file: %s\n", mib_config_file.c_str());
        break;
      }
      case 'y': {
        rrc_setup_config_file = argv[optind];
        printf("Using RRC setup config file: %s\n", rrc_setup_config_file.c_str());
        break;
      }
      case 'Y': {
        rrc_setup_config_size = atoi(argv[optind]);
        printf("Using RRC setup config size: %u\n", rrc_setup_config_size);
        break;
      }
      case 'z': {
        sib_config_file = argv[optind];
        printf("Using sib config file: %s\n", sib_config_file.c_str());
        break;
      }
      case 'Z': {
        sib_config_size = atoi(argv[optind]);
        printf("Using SIB config size: %u\n", sib_config_size);
        break;
      }
      case 'p': {
        config.nof_prb = atoi(argv[optind]);
        printf("Using number of PRBs: %u\n", config.nof_prb);
        break;
      }
      case 'M': {
        config.pdsch_mcs = atoi(argv[optind]);
        printf("Using PDSCH MCS: %u\n", config.pdsch_mcs);
        break;
      }
      case 'P': {
        config.pdsch_prbs = atoi(argv[optind]);
        printf("Using PDSCH PRBs: %u\n", config.pdsch_prbs);
        break;
      }
      default:
        fprintf(stderr, "Unknown option or missing argument.\n");
        exit(EXIT_FAILURE);
    }
  }
  config.duplications = 1;
  config.ssb_pattern  = srsran_ssb_pattern_t::SRSRAN_SSB_PATTERN_C;
  config.duplex_mode  = srsran_duplex_mode_t::SRSRAN_DUPLEX_MODE_TDD;
  config.scs_ssb      = srsran_subcarrier_spacing_t::srsran_subcarrier_spacing_30kHz;
  config.scs_common   = srsran_subcarrier_spacing_t::srsran_subcarrier_spacing_30kHz;
}

int main(int argc, char* argv[])
{
  parse_args(argc, argv);
  /* initialize logger */
  config.log_level             = srslog::basic_levels::debug;
  config.bc_worker_level       = srslog::basic_levels::debug;
  srslog::basic_logger& logger = srslog_init(&config);
  uint32_t              sf_len = config.sample_rate * SF_DURATION;

  /* initialize phy cfg */
  srsran::phy_cfg_nr_t phy_cfg = {};
  init_phy_cfg(phy_cfg, config);

  /* Update phy cfg with mib configurations */

  if (!mib_config_file.empty() && !configure_phy_cfg_from_mib(phy_cfg, mib_config_file, ncellid)) {
    logger.error("Failed to configure phy cfg from mib");
    return -1;
  }
  /* Update phy cfg with sib configurations */
  if (!sib_config_file.empty() && !configure_phy_cfg_from_sib1(phy_cfg, sib_config_file, sib_config_size)) {
    logger.error("Failed to configure phy cfg from SIB1");
    return -1;
  }

  /* Update phy cfg with rrc setup configurations */
  if (!rrc_setup_config_file.empty() &&
      !configure_phy_cfg_from_rrc_setup(phy_cfg, rrc_setup_config_file, rrc_setup_config_size, logger)) {
    logger.error("Failed to configure phy cfg from RRC setup");
    return -1;
  }

  config.source_type             = "file";
  config.source_params           = "/dev/random,/dev/random";
  create_source_t source_creator = load_source(file_source_module_path);
  if (source_creator == nullptr) {
    logger.error("Failed to load source module");
    return -1;
  }
  Source*      source        = source_creator(config);
  GNBDLWorker* gnb_dl_worker = new GNBDLWorker(logger, source, config);
  gnb_dl_worker->init();
  gnb_dl_worker->update_cfg(phy_cfg);

  GNBDLWorker::gnb_dl_task_t task = {};
  task.rnti                       = rnti;
  task.rnti_type                  = srsran_rnti_type_t::srsran_rnti_type_c;
  task.slot_idx                   = 0;
  task.rx_tti                     = 0;
  task.rx_time                    = srsran_timestamp_t();
  task.mcs                        = config.pdsch_mcs;
  task.prbs                       = config.pdsch_prbs;
  task.msg                        = std::make_shared<std::vector<uint8_t> >(std::vector<uint8_t>(100, 0));
  gnb_dl_worker->set_context(task);
  bool success    = gnb_dl_worker->send_pdsch();
  char filename[] = "gnb_dl_worker_test";
  write_record_to_file(gnb_dl_worker->gnb_dl_buffer, sf_len / 2, filename);
}