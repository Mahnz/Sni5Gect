#include "shadower/comp/workers/wd_worker.h"
#include "shadower/utils/utils.h"

WDWorker::WDWorker(srsran_duplex_mode_t duplex_mode_, srslog::basic_levels log_level) : duplex_mode(duplex_mode_)
{
  logger.set_level(log_level);
  /* initialize wdissector and set the dissection mode to FAST */
  wd = wd_init("encap:1");
  wd_set_dissection_mode(wd, WD_MODE_FAST);
}

void WDWorker::process(uint8_t*    data,
                              uint32_t    len,
                              uint16_t    rnti,
                              uint16_t    frame_number,
                              uint16_t    slot_number,
                              uint32_t    slot_idx,
                              direction_t direction,
                              Exploit*    exploit)
{
  /* Only one packet can be processed at a time */
  std::lock_guard<std::mutex> lock(mtx);
  /* apply the fake header to the packet and run the packet dissection */
  int length = add_fake_header(buffer, data, len, rnti, frame_number, slot_number, direction, duplex_mode);
  /* Run pre-dissection to attach the filters */
  exploit->pre_dissection(wd);
  /* Dissect the packet */
  wd_packet_dissect(wd, buffer, length);
  /* Get the summary of the packet */
  const char* summary = wd_packet_summary(wd);
  if (summary) {
    if (direction == DL) {
      logger.info(GREEN "%u [S:%u] --> [P:%s] %s" RESET, rnti, slot_idx, wd_packet_protocol(wd), summary);
    } else {
      logger.info(BLUE "%u [S:%u] <-- [P:%s] %s" RESET, rnti, slot_idx, wd_packet_protocol(wd), summary);
    }
  }
  /* Run post dissection, process after packet is dissected */
  exploit->post_dissection(wd, buffer, length, data, len, direction, slot_idx, logger);
}
