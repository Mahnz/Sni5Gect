#ifndef TEST_VARIABLES_H
#define TEST_VARIABLES_H
#include "shadower/hdr/arg_parser.h"
#include "srsran/phy/common/phy_common_nr.h"
#include <iomanip>
#include <sstream>
/* Test type: 1 srsran, 2 effnet */
#define TEST_TYPE 1

/* Cell id */
#if TEST_TYPE == 1
const uint32_t ncellid = 1;
/* Frequency */
const double dl_freq  = 3427.5e6;
const double ul_freq  = 3427.5e6;
const double ssb_freq = 3421.92e6;
/* Band number */
const uint16_t band = 78;
/* Sample rate */
const double srate = 23.04e6;
/* Band width */
const uint32_t nof_prb = 51;
/* Subcarrier spacing */
srsran_subcarrier_spacing_t scs          = srsran_subcarrier_spacing_30kHz;
int                         slots_per_sf = 1 << (uint32_t)scs;
/* SSB Patter */
srsran_ssb_pattern_t pattern = SRSRAN_SSB_PATTERN_C;
/* RNTI */
const uint16_t ra_rnti = 267;
const uint16_t si_rnti = 0xffff;
const uint16_t c_rnti  = 17921;

std::string mib_config_raw = "shadower/test/data/srsran-n78-20MHz/mib.raw";

std::string sib1_config_raw = "shadower/test/data/srsran-n78-20MHz/sib1.raw";
uint32_t    sib1_size       = 101;

std::string rrc_setup_raw  = "shadower/test/data/srsran-n78-20MHz/rrc_setup.raw";
uint32_t    rrc_setup_size = 316;

std::string rach_msg2_ul_grant_file = "shadower/test/data/srsran-n78-20MHz/rach_msg2_ul_grant.raw";
int32_t     ul_sample_offset        = 468;
double      uplink_cfo              = -0.00054;
#elif TEST_TYPE == 2
const uint32_t ncellid = 1;
/* Frequency */
const double dl_freq  = 3619.2e6;
const double ul_freq  = 3619.2e6;
const double ssb_freq = 3619.2e6;
/* Band number */
const uint16_t band = 78;
/* Sample rate */
const double srate = 23.04e6;
/* Band width */
const uint32_t nof_prb = 51;
/* Subcarrier spacing */
srsran_subcarrier_spacing_t scs          = srsran_subcarrier_spacing_30kHz;
int                         slots_per_sf = 1 << (uint32_t)scs;
/* SSB Patter */
srsran_ssb_pattern_t pattern = SRSRAN_SSB_PATTERN_C;
/* RNTI */
const uint16_t ra_rnti = 0x0113;
const uint16_t si_rnti = 0xffff;
const uint16_t c_rnti  = 42000;

std::string mib_config_raw = "shadower/test/data/mib.raw";

std::string sib1_config_raw = "shadower/test/data/sib1.raw";
uint32_t    sib1_size       = 106;

std::string rrc_setup_raw  = "shadower/test/data/rrc_setup.raw";
uint32_t    rrc_setup_size = 176;

std::string rach_msg2_ul_grant_file = "shadower/test/data/rach_msg2_ul_grant.raw";
int32_t     ul_sample_offset        = 480;
double      uplink_cfo              = 0.0;
#elif TEST_TYPE == 3
const uint32_t ncellid = 1;
/* Frequency */
const double dl_freq  = 3427.5e6;
const double ul_freq  = 3427.5e6;
const double ssb_freq = 3413.28e6;
/* Band number */
const uint16_t band = 78;
/* Sample rate */
const double srate = 46.08e6;
/* Band width */
const uint32_t nof_prb = 106;
/* Subcarrier spacing */
srsran_subcarrier_spacing_t scs          = srsran_subcarrier_spacing_30kHz;
int                         slots_per_sf = 1 << (uint32_t)scs;
/* SSB Patter */
srsran_ssb_pattern_t pattern = SRSRAN_SSB_PATTERN_C;
/* RNTI */
const uint16_t ra_rnti = 267;
const uint16_t si_rnti = 0xffff;
const uint16_t c_rnti  = 17921;

std::string mib_config_raw = "shadower/test/data/srsran-n78-40MHz/mib.raw";

std::string sib1_config_raw = "shadower/test/data/srsran-n78-40MHz/sib1.raw";
uint32_t    sib1_size       = 101;

std::string rrc_setup_raw  = "shadower/test/data/srsran-n78-40MHz/rrc_setup.raw";
uint32_t    rrc_setup_size = 316;

std::string rach_msg2_ul_grant_file = "shadower/test/data/srsran-n78-40MHz/rach_msg2_ul_grant.raw";
int32_t     ul_sample_offset        = 756;
double      uplink_cfo              = -0.00054;
#endif // TEST_TYPE

/* Duplex mode */
srsran_duplex_mode_t duplex = SRSRAN_DUPLEX_MODE_TDD;
/* Buffer length */
const uint32_t sf_len   = srate * SF_DURATION;
const uint32_t slot_len = sf_len / (1 << (uint32_t)scs);
const uint32_t nof_sc   = nof_prb * SRSRAN_NRE;
const uint32_t nof_re   = nof_sc * SRSRAN_NSYMB_PER_SLOT_NR;

ShadowerConfig config = {
    .band                 = band,
    .nof_prb              = nof_prb,
    .scs_common           = scs,
    .scs_ssb              = scs,
    .ra_rnti              = ra_rnti,
    .sample_rate          = srate,
    .tx_gain              = 80,
    .rx_gain              = 60,
    .dl_freq              = dl_freq,
    .ul_freq              = ul_freq,
    .ssb_freq             = ssb_freq,
    .freq_offset          = 0,
    .duplex_mode          = duplex,
    .ssb_pattern          = pattern,
    .bc_worker_log_level  = srslog::basic_levels::debug,
    .slots_to_delay       = 6,
    .send_advance_samples = 160,
    .max_flooding_epoch   = 4,
    .ul_sample_offset     = ul_sample_offset,
    .n_ue_dl_worker       = 4,
    .n_ue_ul_worker       = 4,
    .n_gnb_ul_worker      = 4,
    .n_gnb_dl_worker      = 4,
    .pdsch_mcs            = 0,
    .pdsch_prbs           = 24,
    .close_timeout        = 5000,
    .source_type          = "file",
    .source_params        = "/tmp/output.fc32",
    .pcap_folder          = "/tmp/",
    .enable_recorder      = false,
    .recorder_file        = "output.fc32",
    .log_level            = srslog::basic_levels::info,
};

#endif // TEST_VARIABLES_H