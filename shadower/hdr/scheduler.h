#ifndef SCHEDULER_H
#define SCHEDULER_H
#include "shadower/hdr/arg_parser.h"
#include "shadower/hdr/broadcast_worker.h"
#include "shadower/hdr/safe_queue.h"
#include "shadower/hdr/source.h"
#include "shadower/hdr/syncer.h"
#include "shadower/hdr/thread_pool.h"
#include "shadower/hdr/trace_samples.h"
#include "shadower/hdr/ue_tracker.h"
#include "shadower/hdr/wd_worker.h"
#include "srsran/common/threads.h"
#include "srsran/srslog/srslog.h"
#include <atomic>
class Scheduler : public srsran::thread
{
public:
  Scheduler(ShadowerConfig& config_, Source* source_, Syncer* syncer_, create_exploit_t create_exploit_);
  ~Scheduler() override = default;

private:
  srslog::basic_logger&            logger = srslog::fetch_basic_logger("Scheduler", false);
  ShadowerConfig&                  config;
  Source*                          source           = nullptr;
  Syncer*                          syncer           = nullptr;
  WDWorker*                        wd_worker        = nullptr;
  ThreadPool*                      thread_pool      = nullptr;
  std::shared_ptr<BroadCastWorker> broadcast_worker = nullptr;
  create_exploit_t                 create_exploit;
  SafeQueue<Task>                  task_queue;
  srsran::phy_cfg_nr_t             phy_cfg = {};
  std::atomic<bool>                running{true};

  srsran_mib_nr_t      mib     = {};
  asn1::rrc_nr::sib1_s sib1    = {};
  uint32_t             ncellid = 0;

  std::vector<std::shared_ptr<BroadCastWorker> > broadcast_workers = {};

  void run_thread() override;

  /* Initialize a list of UE trackers before start */
  void pre_initialize_ue();

  /* handler to handle syncer error event */
  void syncer_exit_handler();

  /* handler to handle new received subframe */
  void push_new_task(std::shared_ptr<Task>& task);

  /* handler to activate new UE tracker when new RACH msg2 is found */
  void
  handle_new_ue_found(uint16_t rnti, std::array<uint8_t, 27UL>& grant, uint32_t current_slot, uint32_t time_advance);

  /* handler to apply MIB configuration to multiple workers */
  void handle_mib(srsran_mib_nr_t& mib_, uint32_t ncellid_);

  /* handler to apply sib1 configuration to multiple workers */
  void handle_sib1(asn1::rrc_nr::sib1_s& sib1_);

  void on_ue_deactivate();

  /* list of UE trackers */
  std::vector<std::shared_ptr<UETracker> > ue_trackers = {};
};

#endif // SCHEDULER_H