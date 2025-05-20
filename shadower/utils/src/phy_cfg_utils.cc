#include "shadower/utils/phy_cfg_utils.h"

/* Initialize phy cfg from shadower configuration */
void init_phy_cfg(srsran::phy_cfg_nr_t& phy_cfg, ShadowerConfig& config)
{
  phy_cfg.carrier.dl_center_frequency_hz = config.dl_freq;
  phy_cfg.carrier.ul_center_frequency_hz = config.ul_freq;
  phy_cfg.carrier.ssb_center_freq_hz     = config.ssb_freq;
  phy_cfg.carrier.offset_to_carrier      = 0;
  phy_cfg.carrier.scs                    = config.scs_common;
  phy_cfg.carrier.nof_prb                = config.nof_prb;
  phy_cfg.carrier.max_mimo_layers        = 1;
  phy_cfg.duplex.mode                    = config.duplex_mode;
  phy_cfg.ssb.periodicity_ms             = 10;
  phy_cfg.ssb.position_in_burst[0]       = true;
  phy_cfg.ssb.scs                        = config.scs_ssb;
  phy_cfg.ssb.pattern                    = config.ssb_pattern;
}