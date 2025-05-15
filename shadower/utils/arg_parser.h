#ifndef SNIFFER_HDR_CONFIG_H_
#define SNIFFER_HDR_CONFIG_H_
#include "srsran/common/band_helper.h"
#include "srsran/phy/common/phy_common_nr.h"
#include "srsran/srslog/srslog.h"
#include <boost/program_options.hpp>
#include <boost/program_options/parsers.hpp>
#include <fstream>
#include <iostream>

namespace bpo = boost::program_options;

struct ShadowerConfig {
  // Cell info
  uint16_t                    band;       // Band number
  uint32_t                    nof_prb;    // Number of Physical Resource Blocks
  srsran_subcarrier_spacing_t scs_common; // Subcarrier Spacing for common (kHz)
  srsran_subcarrier_spacing_t scs_ssb;    // Subcarrier Spacing for SSB (kHz)

  // RF info
  uint32_t freq_offset;      // Frequency offset (Hz)
  uint32_t tx_gain;          // Transmit gain (dB)
  uint32_t rx_gain;          // Receive gain (dB)
  uint32_t dl_arfcn;         // Downlink ARFCN
  double   dl_freq;          // Downlink frequency from ARFCN
  uint32_t ul_arfcn;         // Uplink ARFCN
  double   ul_freq;          // Uplink frequency from ARFCN
  uint32_t ssb_arfcn;        // SSB ARFCN
  double   ssb_freq;         // SSB frequency from ARFCN
  double   sample_rate;      // Sample rate (Hz)
  uint32_t nof_channels = 1; // Number of channels
  double   uplink_cfo;       // Uplink CFO correction for PUSCH decoding

  // Derived Cell configurations
  srsran_ssb_pattern_t ssb_pattern;
  srsran_duplex_mode_t duplex_mode;

  // Injector configurations
  uint32_t delay_n_slots;     // Number of slots to delay injecting the message
  uint32_t duplications;      // Number of duplications to send in each inject
  float    tx_cfo_correction; // Uplink CFO correction (Hz)
  int32_t  tx_advancement;    // Number of samples to send in advance, so that on the receiver side, it arrives at the
  uint32_t pdsch_mcs;         // PDSCH MCS used for injection
  uint32_t pdsch_prbs;        // PDSCH PRBs used for injection

  // Worker configurations
  uint32_t n_ue_dl_worker;  // Number of UE downlink workers
  uint32_t n_ue_ul_worker;  // Number of UE uplink workers
  uint32_t n_gnb_ul_worker; // Number of gNB uplink workers
  uint32_t n_gnb_dl_worker; // Number of gNB downlink workers
  uint32_t close_timeout;   // Close timeout, after how long haven't received a message should stop tracking the UE (ms)
  bool     parse_messages;  // Whether we should parse the messages or not
  bool     enable_gpu = false; // Enable GPU acceleration
  size_t   pool_size  = 20;    // Thread pool size
  uint32_t num_ues    = 10;    // Number of UETrackers to pre-initialize

  // Recorder configurations
  bool        enable_recorder = false; // Enable recording the IQ samples to file
  std::string recorder_file;           // Recorder file path

  // Source configurations
  std::string source_type;   // Source type: file, uhd, limeSDR
  std::string source_params; // Source parameters, e.g., device args, record file
  std::string source_module; // Source module file
  double      source_srate;  // Source sample rate (Hz)

  srslog::basic_levels log_level        = srslog::basic_levels::info;
  srslog::basic_levels bc_worker_level  = srslog::basic_levels::info;
  srslog::basic_levels worker_log_level = srslog::basic_levels::info;
  srslog::basic_levels syncer_log_level = srslog::basic_levels::info;

  std::string pcap_folder;    // Pcap folder
  std::string exploit_module; // Exploit module to load
};

inline int parse_args(ShadowerConfig& config, int argc, char* argv[])
{
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
    return SRSRAN_ERROR;
  }
  char*                    config_file = argv[1];
  bpo::options_description common("Config options");
  std::string              log_level;
  common.add_options()
      // clang-format off
      // Cell config section
      ("cell.band",       bpo::value<uint16_t>(&config.band)->default_value(78),    "NR band number by default it is 78")
      ("cell.nof_prb",    bpo::value<uint32_t>(&config.nof_prb)->default_value(51), "Number of Physical Resource Blocks used by the cell")
      ("cell.scs_common", bpo::value<std::string>()->default_value("30"),           "Common subcarrier spacing")
      ("cell.scs_ssb",    bpo::value<std::string>()->default_value("30"),           "SSB subcarrier spacing")
      // RF config section
      ("rf.freq_offset",  bpo::value<uint32_t>(&config.freq_offset)->default_value(0),      "Frequency offset")
      ("rf.tx_gain",      bpo::value<uint32_t>(&config.tx_gain)->default_value(0),          "TX gain")
      ("rf.rx_gain",      bpo::value<uint32_t>(&config.rx_gain)->default_value(40),         "RX gain")
      ("rf.dl_arfcn",     bpo::value<uint32_t>(&config.dl_arfcn)->default_value(628300),    "DL ARFCN")
      ("rf.ssb_arfcn",    bpo::value<uint32_t>(&config.ssb_arfcn)->default_value(628320),   "SSB ARFCN")
      ("rf.sample_rate",  bpo::value<double>(&config.sample_rate)->default_value(23.04e6),  "Sample rate")
      ("rf.uplink_cfo",   bpo::value<double>(&config.uplink_cfo)->default_value(0),         "Uplink CFO to apply")
      // injector configuration section
      ("injector.delay_n_slots",      bpo::value<uint32_t>(&config.delay_n_slots)->default_value(2),    "Number of slots to delay before sending out")
      ("injector.duplications",       bpo::value<uint32_t>(&config.duplications)->default_value(100),   "Number of duplications to inject")
      ("injector.tx_cfo_correction",  bpo::value<float>(&config.tx_cfo_correction)->default_value(0),   "CFO correction before sending out")
      ("injector.tx_advancement",     bpo::value<int32_t>(&config.tx_advancement)->default_value(174),  "Number of samples to send in advance")
      ("injector.pdsch_mcs",          bpo::value<uint32_t>(&config.pdsch_mcs)->default_value(0),        "Modulation and coding scheme used for PDSCH used for injection")
      ("injector.pdsch_prbs",         bpo::value<uint32_t>(&config.pdsch_prbs)->default_value(24),      "Number of physical resource block for PDSCH")
      // Worker configuration section
      ("worker.n_ue_dl_worker",   bpo::value<uint32_t>(&config.n_ue_dl_worker)->default_value(4),     "Number of concurrent ue dl workers")
      ("worker.n_ue_ul_worker",   bpo::value<uint32_t>(&config.n_ue_ul_worker)->default_value(4),     "Number of concurrent ue ul workers")
      ("worker.n_gnb_ul_worker",  bpo::value<uint32_t>(&config.n_gnb_ul_worker)->default_value(4),    "Number of concurrent gnb ul workers")
      ("worker.n_gnb_dl_worker",  bpo::value<uint32_t>(&config.n_gnb_dl_worker)->default_value(4),    "Number of concurrent gnb dl workers")
      ("worker.close_timeout",    bpo::value<uint32_t>(&config.close_timeout)->default_value(30000),  "Number of milliseconds to delay before closing the connection")
      ("worker.parse_messages",   bpo::value<bool>(&config.parse_messages)->default_value(true),      "Prevent parsing messages if the flag is false")
      ("worker.pool_size",        bpo::value<size_t>(&config.pool_size)->default_value(20),           "Pool size")
      ("worker.num_ues",          bpo::value<uint32_t>(&config.num_ues)->default_value(10),           "Number of UEs to pre-initialize")
      ("worker.enable_gpu",       bpo::value<bool>(&config.enable_gpu)->default_value(false),         "Use GPU to accelerate tasks such as FFT")
      // source config section
      ("source.source_type",    bpo::value<std::string>(&config.source_type)->default_value("file"),                "Device args for downlink")
      ("source.source_params",  bpo::value<std::string>(&config.source_params)->default_value("/tmp/output.fc32"),  "Record file for downlink")
      ("source.source_module",  bpo::value<std::string>(&config.source_module)->default_value(""),                  "Module file used for source")
      ("source.source_srate",   bpo::value<double>(&config.source_srate)->default_value(23.04e6),                   "The sample rate of the original source")
      // Pcap settings
      ("pcap.pcap_folder", bpo::value<std::string>(&config.pcap_folder)->default_value("/tmp/"), "Log level")
      // Recorder settings
      ("recorder.enable", bpo::value<bool>(&config.enable_recorder)->default_value(false),              "Enable recorder")
      ("recorder.file",   bpo::value<std::string>(&config.recorder_file)->default_value("output.fc32"), "output FC32 file path")
      // log config section
      ("log.log_level", bpo::value<std::string>()->default_value("INFO"), "Log level")
      ("log.bc_worker", bpo::value<std::string>()->default_value("INFO"), "Broadcast Worker Log level")
      ("log.worker",    bpo::value<std::string>()->default_value("INFO"), "Worker Log level")
      ("log.syncer",    bpo::value<std::string>()->default_value("INFO"), "Syncer Log level")
      // exploit section
      ("exploit.module", bpo::value<std::string>(&config.exploit_module)->default_value(""), "Exploit module");
  // clang-format on

  std::ifstream conf(config_file, std::ios::in);
  if (conf.fail()) {
    std::cerr << "Error: failed to open config file" << std::endl;
    return SRSRAN_ERROR;
  }

  // parse config file and handle errors gracefully
  bpo::variables_map vm;
  try {
    bpo::store(bpo::parse_config_file(conf, common), vm);
    bpo::notify(vm);
  } catch (const boost::program_options::error& e) {
    std::cerr << e.what() << std::endl;
    return SRSRAN_ERROR;
  }

  srsran::srsran_band_helper bands;
  config.scs_common = srsran_subcarrier_spacing_from_str(vm["cell.scs_common"].as<std::string>().c_str());
  config.scs_ssb    = srsran_subcarrier_spacing_from_str(vm["cell.scs_ssb"].as<std::string>().c_str());

  bands.set_scs(config.scs_common);
  config.ul_arfcn         = bands.get_ul_arfcn_from_dl_arfcn(config.dl_arfcn);
  config.dl_freq          = bands.nr_arfcn_to_freq(config.dl_arfcn);
  config.ul_freq          = bands.nr_arfcn_to_freq(config.ul_arfcn);
  config.ssb_freq         = bands.nr_arfcn_to_freq(config.ssb_arfcn);
  double frequency_pointA = bands.get_abs_freq_point_a_from_center_freq(config.nof_prb, config.dl_freq);
  printf("Frequency point A: %f MHz\n", frequency_pointA / 1e6);

  config.ssb_pattern = srsran::srsran_band_helper::get_ssb_pattern(config.band, config.scs_ssb);
  config.duplex_mode = bands.get_duplex_mode(config.band);

  config.log_level        = srslog::str_to_basic_level(vm["log.log_level"].as<std::string>());
  config.syncer_log_level = srslog::str_to_basic_level(vm["log.syncer"].as<std::string>());
  config.bc_worker_level  = srslog::str_to_basic_level(vm["log.bc_worker"].as<std::string>());
  config.worker_log_level = srslog::str_to_basic_level(vm["log.worker"].as<std::string>());
  return SRSRAN_SUCCESS;
}
#endif // SNIFFER_HDR_CONFIG_H_