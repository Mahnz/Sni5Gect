#include "shadower/hdr/syncer.h"
#include "shadower/hdr/utils.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/sync/ssb.h"
#include "srsran/srslog/srslog.h"
#include "test_variables.h"
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <future>
#include <mutex>

#if TEST_TYPE == 1
std::string sample_file = "/root/records/ssb.fc32";
#elif TEST_TYPE == 2
std::string sample_file = "shadower/test/data/sib1.fc32";
#elif TEST_TYPE == 3
std::string sample_file = "shadower/test/data/srsran-n78-40MHz/sib.fc32";
#endif // TEST_TYPE

SafeQueue<Task>                                 task_queue = {};
std::map<uint32_t, std::map<uint32_t, double> > results;
std::mutex                                      mtx;
std::atomic<bool>                               running{false};
std::vector<std::shared_ptr<srsran_ssb_t> >     ssb_list;
float                                           gap = 20e3;

bool on_cell_found(srsran_mib_nr_t& mib, uint32_t ncellid_)
{
  std::array<char, 512> mib_info_str = {};
  srsran_pbch_msg_nr_mib_info(&mib, mib_info_str.data(), (uint32_t)mib_info_str.size());
  printf("Found cell: %s %u\n", mib_info_str.data(), ncellid_);
  return true;
}

void syncer_exit_handler()
{
  running.store(false);
}

// Handler for syncer to push new task to the task queue
void push_new_task(std::shared_ptr<Task>& task)
{
  task_queue.push(task);
}

bool search_ssb(std::shared_ptr<srsran_ssb_t> ssb, std::shared_ptr<Task> task)
{
  /* Search for SSB */
  srsran_ssb_search_res_t res = {};
  if (srsran_ssb_search(ssb.get(), task->buffer->data(), task->buffer->size(), &res)) {
    return false;
  }

  if (!res.pbch_msg.crc) {
    return false;
  }
  if (res.N_id != 1) {
    return false;
  }
  // printf("Task id: %u SSB Freq: %f CFO: %f\n", task->task_idx, ssb->cfg.ssb_freq_hz / 1e6, res.measurements.cfo_hz);
  std::lock_guard<std::mutex> lock(mtx);
  uint32_t                    ssb_freq = (uint32_t)ssb->cfg.ssb_freq_hz;
  results[task->task_idx][ssb_freq]    = res.measurements.cfo_hz;
  return true;
}

void task_processor()
{
  for (uint32_t i = 0; i < 1000; i++) {
    std::shared_ptr<Task> task = task_queue.retrieve();
    if (task && task->slot_idx % 10 == 0) {
      std::vector<std::future<bool> > futures;
      for (auto& ssb : ssb_list) {
        futures.push_back(std::async(std::launch::async, search_ssb, ssb, task));
      }
      bool all_success = false;
      for (auto& fut : futures) {
        all_success &= fut.get();
      }
      // printf("\n");

      std::map<uint32_t, double>& cfo_list = results[task->task_idx];
      uint32_t                    min = 4230196224, max = 0;
      for (std::map<uint32_t, double>::const_iterator it = cfo_list.begin(); it != cfo_list.end(); ++it) {
        if (std::abs(it->second) < 100.0) {
          if (it->first < min) {
            min = it->first;
          }
          if (it->first > max) {
            max = it->first;
          }
        }
        // printf("Task id: %u SSB Freq: %f CFO: %f\n", task->task_idx, it->first / 1e6, it->second);
      }
      if (min < 4000e6) {
        float mid  = (min / 1e3 + max / 1e3) / 2;
        float diff = (mid * 1e3 - config.ssb_freq) / 1e3;
        printf("Task id: %u Min: %f max: %f mid: %f diff: %f\n", task->task_idx, min / 1e6, max / 1e6, mid / 1e3, diff);
      }
    }
  }
}

int main(int argc, char* argv[])
{
  /* initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::debug);
  logger.info("SSB target frequency: %f", ssb_freq / 1e6);

  for (double freq = config.ssb_freq - gap; freq < config.ssb_freq + gap; freq += 1e3) {
    /* initialize ssb */
    std::shared_ptr<srsran_ssb_t> ssb = std::make_shared<srsran_ssb_t>();
    if (!init_ssb(
            *ssb, config.sample_rate, config.dl_freq, freq, config.scs_ssb, config.ssb_pattern, config.duplex_mode)) {
      logger.error("Failed to initialize SSB");
    }
    ssb_list.push_back(ssb);
  }

  /* Initialize syncer args */
  syncer_args_t syncer_args = {
      .srate       = config.sample_rate,
      .scs         = config.scs_ssb,
      .dl_freq     = config.dl_freq,
      .ssb_freq    = config.ssb_freq,
      .pattern     = config.ssb_pattern,
      .duplex_mode = config.duplex_mode,
  };

  create_source_t limesdr_source = load_source(limesdr_source_module_path);
  config.source_params =
      "logLevel:3,freq_corr:34500,port0:\"dev0\",dev0:\"XTRX\",dev0_chipIndex:0,"
      "dev0_linkFormat:\"I12\",dev0_rx_path:\"LNAH\",dev0_tx_path:\"Band1\","
      "dev0_max_channels_to_use:1,dev0_calibration:\"none\",dev0_rx_gfir_enable:0,dev0_tx_gfir_enable:0";
  config.rx_gain         = 50;
  config.enable_recorder = true;
  Source* source         = limesdr_source(config);

  std::thread t(task_processor);

  /* Initialize syncer */
  Syncer* syncer = new Syncer(syncer_args, source, config);
  syncer->init();
  syncer->on_cell_found    = std::bind(on_cell_found, std::placeholders::_1, std::placeholders::_2);
  syncer->error_handler    = std::bind(syncer_exit_handler);
  syncer->publish_subframe = std::bind(push_new_task, std::placeholders::_1);
  syncer->start(0);
  syncer->wait_thread_finish();
  source->close();
  if (t.joinable()) {
    t.join();
  }
  return 0;
}