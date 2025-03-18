#include "shadower/hdr/source.h"
#include "shadower/hdr/syncer.h"
#include "shadower/hdr/utils.h"
#include "test_variables.h"
SafeQueue<Task> task_queue = {};

bool on_cell_found(srsran_mib_nr_t& mib, uint32_t ncellid_)
{
  std::array<char, 512> mib_info_str = {};
  srsran_pbch_msg_nr_mib_info(&mib, mib_info_str.data(), (uint32_t)mib_info_str.size());
  printf("Found cell: %s %u\n", mib_info_str.data(), ncellid_);
  return true;
}

void syncer_exit_handler() {}

// Handler for syncer to push new task to the task queue
void push_new_task(std::shared_ptr<Task>& task)
{
  task_queue.push(task);
}

int main()
{
  config.enable_recorder  = false;
  config.syncer_log_level = srslog::basic_levels::debug;

  /* initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::debug);

  /* Initialize syncer args */
  syncer_args_t syncer_args = {
      .srate       = config.sample_rate,
      .scs         = config.scs_ssb,
      .dl_freq     = config.dl_freq,
      .ssb_freq    = config.ssb_freq,
      .pattern     = config.ssb_pattern,
      .duplex_mode = config.duplex_mode,
  };
  /* Initialize source */
  // config.source_params        = "records/example.fc32";
  // create_source_t file_source = load_source(file_source_module_path);
  // Source*         source      = file_source(config);

  create_source_t uhd_source = load_source(uhd_source_module_path);
  config.source_params       = "type=b200";
  Source* source             = uhd_source(config);

  /* Initialize syncer */
  Syncer* syncer = new Syncer(syncer_args, source, config);
  syncer->init();
  syncer->on_cell_found    = std::bind(on_cell_found, std::placeholders::_1, std::placeholders::_2);
  syncer->error_handler    = std::bind(syncer_exit_handler);
  syncer->publish_subframe = std::bind(push_new_task, std::placeholders::_1);
  syncer->start(0);
  syncer->wait_thread_finish();
  source->close();
}