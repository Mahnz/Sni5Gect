#include "srsran/phy/sync/ssb.h"

/* Helper function to initialize ssb */
bool init_ssb(srsran_ssb_t&               ssb,
              double                      srate,
              double                      dl_freq,
              double                      ssb_freq,
              srsran_subcarrier_spacing_t scs,
              srsran_ssb_pattern_t        pattern,
              srsran_duplex_mode_t        duplex_mode);