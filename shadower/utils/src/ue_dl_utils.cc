#include "shadower/utils/ue_dl_utils.h"

/* ue_dl related configuration and update, ue_dl decode messages send from base station to UE*/
bool init_ue_dl(srsran_ue_dl_nr_t& ue_dl, cf_t* buffer, srsran::phy_cfg_nr_t& phy_cfg)
{
  srsran_ue_dl_nr_args_t ue_dl_args             = {};
  ue_dl_args.nof_max_prb                        = phy_cfg.carrier.nof_prb;
  ue_dl_args.nof_rx_antennas                    = 1;
  ue_dl_args.pdcch.measure_evm                  = false;
  ue_dl_args.pdcch.measure_time                 = false;
  ue_dl_args.pdcch.disable_simd                 = false;
  ue_dl_args.pdsch.sch.disable_simd             = false;
  ue_dl_args.pdsch.sch.decoder_use_flooded      = false;
  ue_dl_args.pdsch.sch.decoder_scaling_factor   = 0;
  ue_dl_args.pdsch.sch.max_nof_iter             = 10;
  std::array<cf_t*, SRSRAN_MAX_PORTS> rx_buffer = {};
  rx_buffer[0]                                  = buffer;
  if (srsran_ue_dl_nr_init(&ue_dl, rx_buffer.data(), &ue_dl_args) != 0) {
    return false;
  }
  if (!update_ue_dl(ue_dl, phy_cfg)) {
    return false;
  }
  return true;
}

bool update_ue_dl(srsran_ue_dl_nr_t& ue_dl, srsran::phy_cfg_nr_t& phy_cfg)
{
  if (srsran_ue_dl_nr_set_carrier(&ue_dl, &phy_cfg.carrier) != SRSRAN_SUCCESS) {
    return false;
  }
  srsran_dci_cfg_nr_t dci_cfg = phy_cfg.get_dci_cfg();
  if (srsran_ue_dl_nr_set_pdcch_config(&ue_dl, &phy_cfg.pdcch, &dci_cfg) != SRSRAN_SUCCESS) {
    return false;
  }
  return true;
}