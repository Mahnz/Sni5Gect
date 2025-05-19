#ifndef TASK_H
#define TASK_H
#include "srsran/phy/common/timestamp.h"
#include <memory>
#include <vector>

struct Task {
  std::shared_ptr<std::vector<cf_t> > dl_buffer[SRSRAN_MAX_CHANNELS];
  std::shared_ptr<std::vector<cf_t> > ul_buffer[SRSRAN_MAX_CHANNELS];
  std::shared_ptr<std::vector<cf_t> > last_dl_buffer[SRSRAN_MAX_CHANNELS];
  std::shared_ptr<std::vector<cf_t> > last_ul_buffer[SRSRAN_MAX_CHANNELS];
  uint32_t                            slot_idx;
  srsran_timestamp_t                  ts;
  uint32_t                            task_idx;
};
#endif // TASK_H