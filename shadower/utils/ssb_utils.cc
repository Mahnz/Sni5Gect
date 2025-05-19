#include "shadower/utils/utils.h"

/* Helper function to initialize ssb */
bool init_ssb(srsran_ssb_t&               ssb,
              double                      srate,
              double                      dl_freq,
              double                      ssb_freq,
              srsran_subcarrier_spacing_t scs,
              srsran_ssb_pattern_t        pattern,
              srsran_duplex_mode_t        duplex_mode)
{
  srsran_ssb_args_t ssb_args = {};
  ssb_args.max_srate_hz      = srate;
  ssb_args.min_scs           = scs;
  ssb_args.enable_search     = true;
  ssb_args.enable_measure    = true;
  ssb_args.enable_decode     = true;
  if (srsran_ssb_init(&ssb, &ssb_args) != 0) {
    printf("Error initialize ssb\n");
    return false;
  }
  srsran_ssb_cfg_t ssb_cfg = {};
  ssb_cfg.srate_hz         = srate;
  ssb_cfg.center_freq_hz   = dl_freq;
  ssb_cfg.ssb_freq_hz      = ssb_freq;
  ssb_cfg.scs              = scs;
  ssb_cfg.pattern          = pattern;
  ssb_cfg.duplex_mode      = duplex_mode;
  ssb_cfg.periodicity_ms   = 10;
  if (srsran_ssb_set_cfg(&ssb, &ssb_cfg) < SRSRAN_SUCCESS) {
    printf("Error set srsran_ssb_set_cfg\n");
    return false;
  }
  return true;
}