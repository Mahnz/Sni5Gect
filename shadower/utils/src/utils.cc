#include "shadower/utils/utils.h"
#include <chrono>
#include <fstream>

/* Write the IQ samples to a file so that we can use tools like matlab or spectrogram-py to debug */
void write_record_to_file(cf_t* buffer, uint32_t length, char* name, const std::string& folder)
{
  char filename[256];
  if (name) {
    sprintf(filename, "%s/%s.fc32", folder.c_str(), name);
  } else {
    auto now                     = std::chrono::high_resolution_clock::now();
    auto nanoseconds_since_epoch = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    sprintf(filename, "%s/record_%ld.fc32", folder.c_str(), nanoseconds_since_epoch);
  }
  std::ofstream f(filename, std::ios::binary);
  if (f) {
    f.write(reinterpret_cast<char*>(buffer), length * sizeof(cf_t));
    f.close();
  } else {
    printf("Error opening file: %s\n", filename);
  }
}

/* Load the IQ samples from a file */
bool load_samples(const std::string& filename, cf_t* buffer, size_t nsamples)
{
  std::ifstream infile(filename, std::ios::binary);
  if (!infile.is_open()) {
    return false;
  }
  infile.read(reinterpret_cast<char*>(buffer), nsamples * sizeof(cf_t));
  infile.close();
  return true;
}

/* Read binary form configuration dumped structure */
bool read_raw_config(const std::string& filename, uint8_t* buffer, size_t size)
{
  std::ifstream infile(filename, std::ios::binary);
  if (!infile.is_open()) {
    return false;
  }
  infile.read(reinterpret_cast<char*>(buffer), size);
  return true;
}

/* Initialize logger */
srslog::basic_logger& srslog_init(ShadowerConfig* config)
{
  srslog::init();
  srslog::sink* sink        = nullptr;
  sink                      = srslog::create_stdout_sink();
  srslog::log_channel* chan = srslog::create_log_channel("main", *sink);
  srslog::set_default_sink(*sink);
  srslog::basic_logger& logger = srslog::fetch_basic_logger("main", false);
  logger.set_level(config->log_level);
  return logger;
}

/* Calculate the RA-rnti from SIB1 configuration */
std::vector<uint16_t> get_ra_rnti_list(asn1::rrc_nr::sib1_s& sib1, ShadowerConfig& config)
{
  std::vector<uint16_t>    ra_rnti_list;
  std::vector<uint16_t>    t_idx_list;
  const prach_nr_config_t* prach_cfg_nr;
  uint16_t                 ul_carrier_id;
  uint32_t                 num_ra_rnti = 0;

  if (!sib1.serving_cell_cfg_common_present || !sib1.serving_cell_cfg_common.ul_cfg_common_present ||
      !sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.rach_cfg_common_present) {
    return ra_rnti_list;
  }

  /* Get the prach configuration index */
  uint16_t prach_cfg_idx =
      sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.rach_cfg_common.setup().rach_cfg_generic.prach_cfg_idx;

  /* Retrieve the PRACH config using cfg idx */
  if (config.duplex_mode == SRSRAN_DUPLEX_MODE_TDD) {
    prach_cfg_nr  = srsran_prach_nr_get_cfg_fr1_unpaired(prach_cfg_idx);
    ul_carrier_id = 0;
  } else if (config.duplex_mode == SRSRAN_DUPLEX_MODE_FDD) {
    prach_cfg_nr  = srsran_prach_nr_get_cfg_fr1_paired(prach_cfg_idx);
    ul_carrier_id = 0;
  } else {
    return ra_rnti_list;
  }

  /* Get the number of RA-RNTI */
  num_ra_rnti = prach_cfg_nr->nof_subframe_number;
  t_idx_list.resize(prach_cfg_nr->nof_subframe_number);
  ra_rnti_list.resize(prach_cfg_nr->nof_subframe_number);

  /* Get the list of t_idx */
  if (sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.rach_cfg_common.setup().prach_root_seq_idx.type() == 0) {
    for (uint32_t t_idx_id = 0; t_idx_id < prach_cfg_nr->nof_subframe_number; t_idx_id++) {
      t_idx_list[t_idx_id] = prach_cfg_nr->subframe_number[t_idx_id];
    }
  } else if (sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.rach_cfg_common.setup().prach_root_seq_idx.type() ==
             1) {
    uint32_t slots_per_sf = SRSRAN_NSLOTS_PER_SF_NR(config.scs_common);
    t_idx_list.resize(prach_cfg_nr->nof_subframe_number * slots_per_sf);
    ra_rnti_list.resize(prach_cfg_nr->nof_subframe_number * slots_per_sf);
    num_ra_rnti = prach_cfg_nr->nof_subframe_number * slots_per_sf;
    for (uint32_t t_idx_id = 0; t_idx_id < prach_cfg_nr->nof_subframe_number; t_idx_id++) {
      for (uint32_t slot_idx = 0; slot_idx < slots_per_sf; slot_idx++) {
        t_idx_list[t_idx_id * slots_per_sf + slot_idx] =
            prach_cfg_nr->subframe_number[t_idx_id] * slots_per_sf + slot_idx;
      }
    }
  } else {
    return ra_rnti_list;
  }

  /*  TS - 38.321: 5.1.3 Random Access Preamble transmission
    RA-RNTI = 1 + s_id + 14 × t_id + 14 × 80 × f_id + 14 × 80 × 8 × ul_carrier_id.
    s_id is the index of the first OFDM symbol of the PRACH occasion (0 ≤ s_id < 14)
    t_id is the index of the first slot of the PRACH occasion in a system frame (0 ≤ t_id < 80)
    subcarrier spacing to determine t_id is based on the value of μ specified in clause 5.3.2 in TS 38.211
    f_id is the index of the PRACH occasion in the frequency domain (0 ≤ f_id < 8)
    ul_carrier_id is the UL carrier used for Random Access Preamble transmission (0 for NUL carrier, and 1 for SUL
    carrier)
    */
  for (uint32_t i = 0; i < num_ra_rnti; i++) {
    uint16_t s_id = prach_cfg_nr->starting_symbol;
    uint16_t t_id = t_idx_list[i];
    uint16_t f_id = 0;

    uint16_t ra_rnti = 1 + s_id + 14 * t_id + 14 * 80 * f_id + 14 * 80 * 8 * ul_carrier_id;
    ra_rnti_list[i]  = ra_rnti;
  }
  return ra_rnti_list;
}