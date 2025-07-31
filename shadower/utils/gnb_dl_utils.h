#pragma once
#include "srsran/common/phy_cfg_nr.h"
#include "srsran/phy/gnb/gnb_dl.h"
#include "srsran/srslog/srslog.h"

/* gnb_dl related configuration and update, gnb_dl encode messages send from base station to UE */
bool init_gnb_dl(srsran_gnb_dl_t& gnb_dl, cf_t* buffer, srsran::phy_cfg_nr_t& phy_cfg, double srate);
bool update_gnb_dl(srsran_gnb_dl_t& gnb_dl, srsran::phy_cfg_nr_t& phy_cfg);

/* use gnb_dl to encode the message targeted to UE */
bool gnb_dl_encode(std::shared_ptr<std::vector<uint8_t> > msg,
                   srsran_gnb_dl_t&                       gnb_dl,
                   srsran_dci_cfg_nr_t&                   dci_cfg,
                   srsran::phy_cfg_nr_t&                  phy_cfg,
                   srsran_sch_cfg_nr_t&                   pdsch_cfg,
                   srsran_slot_cfg_t&                     slot_cfg,
                   uint16_t                               rnti,
                   srsran_rnti_type_t                     rnti_type,
                   srslog::basic_logger&                  logger,
                   uint32_t                               mcs,
                   uint32_t                               nof_prb_to_allocate);
