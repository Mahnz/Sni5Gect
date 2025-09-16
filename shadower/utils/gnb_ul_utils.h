#pragma once
#include "srsran/common/phy_cfg_nr.h"
#include "srsran/phy/gnb/gnb_ul.h"
#include "srsran/srslog/srslog.h"

/* gnb_ul related configuration and update, gnb_ul decode messages send from UE to base station */
bool init_gnb_ul(srsran_gnb_ul_t& gnb_ul, cf_t* buffer, srsran::phy_cfg_nr_t& phy_cfg);
bool update_gnb_ul(srsran_gnb_ul_t& gnb_ul, srsran::phy_cfg_nr_t& phy_cfg);

/* Detect and decode PUSCH info bytes */
bool gnb_ul_pusch_decode(srsran_gnb_ul_t&        gnb_ul,
                         srsran_sch_cfg_nr_t&    pusch_cfg,
                         srsran_slot_cfg_t&      slot_cfg,
                         srsran_pusch_res_nr_t&  pusch_res,
                         srsran_softbuffer_rx_t& softbuffer_rx,
                         srslog::basic_logger&   logger,
                         uint32_t                task_idx = 0);