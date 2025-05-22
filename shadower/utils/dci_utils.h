#include "srsran/common/phy_cfg_nr.h"
#include "srsran/phy/common/phy_common_nr.h"
#include "srsran/phy/phch/dci_nr.h"

/* Construct the dci to send */
bool construct_dci_ul_to_send(srsran_dci_ul_nr_t&   dci_to_send,
                              srsran::phy_cfg_nr_t& phy_cfg,
                              uint32_t              slot_idx,
                              uint16_t              rnti,
                              srsran_rnti_type_t    rnti_type,
                              uint32_t              mcs,
                              uint32_t              nof_prb_to_allocate);

bool construct_dci_dl_to_send(srsran_dci_dl_nr_t&   dci_to_send,
                              srsran::phy_cfg_nr_t& phy_cfg,
                              uint32_t              slot_idx,
                              uint16_t              rnti,
                              srsran_rnti_type_t    rnti_type,
                              uint32_t              mcs,
                              uint32_t              nof_prb_to_allocate);

/* Find a search space that contains target dci format */
bool find_search_space(srsran_search_space_t** search_space,
                       srsran::phy_cfg_nr_t&   phy_cfg,
                       srsran_dci_format_nr_t  format);

/* Find an aggregation level to use */
bool find_aggregation_level(srsran_dci_ctx_t&      dci_ctx,
                            srsran_coreset_t*      coreset,
                            srsran_search_space_t* search_space,
                            uint32_t               slot_idx,
                            uint16_t               rnti);