#include "srsran/common/phy_cfg_nr.h"
#include "srsran/phy/ue/ue_dl_nr.h"

/* ue_dl related configuration and update, ue_dl decode messages send from base station to UE*/
bool init_ue_dl(srsran_ue_dl_nr_t& ue_dl, cf_t* buffer, srsran::phy_cfg_nr_t& phy_cfg);

bool update_ue_dl(srsran_ue_dl_nr_t& ue_dl, srsran::phy_cfg_nr_t& phy_cfg);