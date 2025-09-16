#include "shadower/comp/scheduler.h"
#include "shadower/comp/sync/syncer.h"
#include "shadower/modules/exploit.h"
#include "shadower/source/source.h"
#include "shadower/utils/arg_parser.h"
#include "srsran/srslog/srslog.h"

int main(int argc, char* argv[])
{
  // Configuration for the overshadower
  ShadowerConfig config = {};
  // load configurations from file
  if (parse_args(config, argc, argv)) {
    printf("Error parsing args\n");
    return -1;
  }
  srslog::basic_logger& logger = srslog_init(&config);
  logger.set_level(config.log_level);

  // Wait 100ms to allow the logger to initialize
  usleep(100000);

  // Source pointer
  Source*         source        = nullptr;
  create_source_t create_source = nullptr;
  // Initialize the source based on the configuration
  if (config.source_type == "file") {
    std::string module_path = config.source_module.empty() ? config.source_module : file_source_module_path;
    create_source           = load_source(module_path);
  } else if (config.source_type == "uhd") {
    std::string module_path = config.source_module.empty() ? config.source_module : uhd_source_module_path;
    create_source           = load_source(module_path);
  } else if (config.source_type == "limesdr") {
    std::string module_path = config.source_module.empty() ? config.source_module : limesdr_source_module_path;
    create_source           = load_source(module_path);
  } else {
    logger.error("Invalid source type: %s", config.source_type.c_str());
    return -1;
  }

  /* Create the actual source */
  source = create_source(config);
  // load exploit module
  create_exploit_t exploit_creator = load_exploit(config.exploit_module);
  logger.info(YELLOW "Loaded exploit module: %s" RESET, config.exploit_module.c_str());
  // Initialize and create the syncer instance
  syncer_args_t syncer_args = {
      .srate       = config.sample_rate,
      .scs         = config.scs_ssb,
      .dl_freq     = config.dl_freq,
      .ssb_freq    = config.ssb_freq,
      .pattern     = config.ssb_pattern,
      .duplex_mode = config.duplex_mode,
  };
  Syncer* syncer = new Syncer(syncer_args, source, config);
  if (!syncer->init()) {
    logger.error(YELLOW "Error initializing syncer" RESET);
    return -1;
  }
  logger.info(YELLOW "Initialized syncer" RESET);

  // Initialize the scheduler
  Scheduler* scheduler = new Scheduler(config, source, syncer, exploit_creator);
  logger.info(YELLOW "Initialized scheduler" RESET);
  // Start the syncer thread with the highest priority
  syncer->start(0);
  // Start the scheduler thread with lower priority
  scheduler->start(4);

  syncer->wait_thread_finish();
  source->close();
  return 0;
}