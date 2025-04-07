extern "C" {
#include "srsran/phy/sync/sss_nr.h"
}
#include "shadower/hdr/safe_queue.h"
#include "shadower/hdr/syncer.h"
#include "shadower/hdr/utils.h"
#include "srsran/mac/mac_rar_pdu_nr.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/sync/ssb.h"
#include "srsran/phy/ue/ue_dl_nr.h"
#include "test_variables.h"
#include <dirent.h>
#include <fstream>
#include <sys/types.h>
SafeQueue<Task>   task_queue = {};
std::atomic<bool> running{true};
std::atomic<bool> cell_found{false};
double            test_ssb_freq = 3424.80e6;
std::string       sdr_args      = "type=b200";
uint32_t          ssb_offset    = 1650;
uint32_t          advancement   = 9;
uint32_t          test_round    = 1000;

/* When a cell is found log the cell information */
bool on_cell_found(srsran_mib_nr_t& mib, uint32_t ncellid)
{
  std::array<char, 512> mib_info_str = {};
  srsran_pbch_msg_nr_mib_info(&mib, mib_info_str.data(), (uint32_t)mib_info_str.size());
  printf("Found cell: %s\n", mib_info_str.data());
  cell_found = true;
  return true;
}

/* Syncer function push new task to the queue */
void push_new_task(std::shared_ptr<Task>& task)
{
  task_queue.push(task);
}

/* Exit on syncer error, and also stop the sender thread */
void handle_syncer_exit(Syncer* syncer, std::thread& sender, std::thread& receiver)
{
  running = false;
  syncer->thread_cancel();
  pthread_cancel(sender.native_handle());
  pthread_cancel(receiver.native_handle());
}

void sender_thread(srslog::basic_logger& logger, uint32_t slot_len, Source* source, Syncer* syncer)
{
  uint32_t                            last_sent = 0;
  std::string                         filename  = "shadower/test/data/ssb.fc32";
  std::shared_ptr<std::vector<cf_t> > samples   = std::make_shared<std::vector<cf_t> >(slot_len);
  /* Load the ssb sample from the file */
  if (!load_samples(filename, samples->data(), slot_len)) {
    logger.error("Failed to load data from %s", filename.c_str());
    return;
  }

  while (running) {
    /* Wait the cell be found first, we are aiming to matching the time of a base station */
    if (!cell_found) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    uint32_t           current_idx = 0;
    srsran_timestamp_t timestamp   = {};
    syncer->get_tti(&current_idx, &timestamp);

    /* If we have send the SSB in this subframe, then skip*/
    if (current_idx == last_sent) {
      std::this_thread::sleep_for(std::chrono::microseconds(300));
      continue;
    }

    /* Send the samples a few slots later */
    uint32_t target_slot_idx = current_idx + advancement;
    srsran_timestamp_add(&timestamp, 0, advancement * 5e-4);
    source->send(samples->data(), slot_len, timestamp, target_slot_idx);
    last_sent = current_idx;
  }
}

void receiver_thread(srsran_ssb_t& ssb, srslog::basic_logger& logger, ShadowerConfig& config, double test_freq)
{
  std::shared_ptr<std::vector<cf_t> > buffer = std::make_shared<std::vector<cf_t> >(config.sample_rate * SF_DURATION);
  std::map<int32_t, uint32_t>         delay_map;
  uint32_t                            count     = 0;
  double                              total_cfo = 0;
  while (running) {
    /* Retrieve the well aligned slots */
    if (!cell_found) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    std::shared_ptr<Task> task = task_queue.retrieve();
    if (task == nullptr) {
      continue;
    }
    if (!task->buffer) {
      continue;
    }

    /* Run SSB search on the received slots */
    srsran_ssb_search_res_t res = {};
    if (srsran_ssb_search(&ssb, task->buffer->data() + slot_len, slot_len, &res) != 0) {
      logger.error("Failed to search ssb");
      continue;
    }
    if (res.measurements.snr_dB < -10.0f || res.measurements.cfo_hz == 0) {
      continue;
    }
    if (res.N_id != ncellid) {
      continue;
    }

    int32_t delay_samples = res.t_offset - ssb_offset;
    if (delay_samples > 1000 || delay_samples < -1000) {
      continue;
    }
    total_cfo += res.measurements.cfo_hz;
    /* Count the delays */
    if (delay_map.find(delay_samples) == delay_map.end()) {
      delay_map[delay_samples] = 1;
    } else {
      delay_map[delay_samples]++;
    }
    if (count++ % test_round == 0 && count > 1) {
      /* Log the static information */
      int32_t total = 0;
      int32_t min   = 10000;
      int32_t max   = 0;
      for (const auto& pair : delay_map) {
        logger.info("Delay: %d Count: %u", pair.first, pair.second);
        if (pair.first < min) {
          min = pair.first;
        }
        if (pair.first > max) {
          max = pair.first;
        }
        total += pair.first * pair.second;
      }
      logger.info("Min: %d Max: %d Avg: %d", min, max, total / count);
      logger.info("Avg CFO: %f", total_cfo / count);
    }
  }
}

int ssb_generate(srsran_ssb_t&         ssb,
                 srsran_ssb_cfg_t&     ssb_cfg,
                 ShadowerConfig&       config,
                 uint32_t              ncellid,
                 srslog::basic_logger& logger)
{
  /* initialize phy cfg */
  srsran::phy_cfg_nr_t phy_cfg = {};
  init_phy_cfg(phy_cfg, config);

  /* load mib configuration and update phy_cfg */
  if (!configure_phy_cfg_from_mib(phy_cfg, mib_config_raw, ncellid)) {
    printf("Failed to configure phy cfg from mib\n");
    return -1;
  }

  /* load sib1 configuration and apply to phy_cfg */
  if (!configure_phy_cfg_from_sib1(phy_cfg, sib1_config_raw, sib1_size)) {
    logger.error("Failed to configure phy cfg from sib1");
    return -1;
  }

  /* load rrc_setup cell configuration and apply to phy_cfg */
  if (!configure_phy_cfg_from_rrc_setup(phy_cfg, rrc_setup_raw, rrc_setup_size, logger)) {
    logger.error("Failed to configure phy cfg from rrc setup");
    return -1;
  }

  /* GNB DL init with configuration from phy_cfg */
  srsran_gnb_dl_t gnb_dl        = {};
  cf_t*           gnb_dl_buffer = srsran_vec_cf_malloc(sf_len);
  if (!init_gnb_dl(gnb_dl, gnb_dl_buffer, phy_cfg, config.sample_rate)) {
    logger.error("Failed to init GNB DL");
    return -1;
  }

  /* add ssb config to gnb_dl */
  srsran_gnb_dl_set_ssb_config(&gnb_dl, &ssb_cfg);
  srsran_pbch_msg_nr_t pbch_msg = {};
  if (srsran_gnb_dl_add_ssb(&gnb_dl, &pbch_msg, 0) < SRSRAN_SUCCESS) {
    logger.error("Failed to add SSB");
    return -1;
  }

  char filename[64];
  sprintf(filename, "ssb");
  write_record_to_file(gnb_dl_buffer, sf_len, filename, "shadower/test/data");
  return 0;
}

int main(int argc, char* argv[])
{
  /* initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::debug);

  /* Initialize source */

  create_source_t uhd_source = load_source(uhd_source_module_path);
  config.source_params       = sdr_args;
  Source* source             = uhd_source(config);

  // create_source_t limesdr_source = load_source(limesdr_source_module_path);
  // config.source_params =
  //     "logLevel:5,port0:\"dev0\",dev0:\"XTRX\",dev0_chipIndex:0,"
  //     "dev0_linkFormat:\"I12\",dev0_rx_path:\"LNAH\",dev0_tx_path:\"Band1\","
  //     "dev0_max_channels_to_use:1,dev0_calibration:\"none\",dev0_rx_gfir_enable:0,dev0_tx_gfir_enable:0";
  // config.rx_gain = 50;
  // config.tx_gain = 50;
  // Source* source = limesdr_source(config);
  logger.info("Selected target test SSB frequency %.3f MHz", test_ssb_freq / 1e6);

  /* initialize SSB */
  srsran_ssb_t      ssb      = {};
  srsran_ssb_args_t ssb_args = {};
  ssb_args.max_srate_hz      = config.sample_rate;
  ssb_args.min_scs           = config.scs_ssb;
  ssb_args.enable_search     = true;
  ssb_args.enable_measure    = true;
  ssb_args.enable_decode     = true;
  ssb_args.enable_encode     = true;
  if (srsran_ssb_init(&ssb, &ssb_args) != 0) {
    logger.error("Failed to initialize ssb");
    return -1;
  }

  /* Set SSB config */
  srsran_ssb_cfg_t ssb_cfg = {};
  ssb_cfg.srate_hz         = config.sample_rate;
  ssb_cfg.center_freq_hz   = config.dl_freq;
  ssb_cfg.ssb_freq_hz      = test_ssb_freq;
  ssb_cfg.scs              = config.scs_ssb;
  ssb_cfg.pattern          = config.ssb_pattern;
  ssb_cfg.duplex_mode      = config.duplex_mode;
  ssb_cfg.periodicity_ms   = 10;
  if (srsran_ssb_set_cfg(&ssb, &ssb_cfg) < SRSRAN_SUCCESS) {
    logger.error("Failed to set ssb config");
    return -1;
  }

  /* generate the SSB signals */
  if (ssb_generate(ssb, ssb_cfg, config, ncellid, logger) != 0) {
    logger.error("Failed to generate SSB signals");
    return -1;
  }

  /* Initialize syncer */
  syncer_args_t syncer_args = {
      .srate       = config.sample_rate,
      .scs         = config.scs_ssb,
      .dl_freq     = config.dl_freq,
      .ssb_freq    = config.ssb_freq,
      .pattern     = config.ssb_pattern,
      .duplex_mode = config.duplex_mode,
  };
  Syncer* syncer = new Syncer(syncer_args, source, config);
  syncer->init();
  syncer->on_cell_found    = std::bind(on_cell_found, std::placeholders::_1, std::placeholders::_2);
  syncer->publish_subframe = std::bind(push_new_task, std::placeholders::_1);
  /* Sender thread keep sending SSB blocks */
  std::thread sender(sender_thread, std::ref(logger), slot_len, source, syncer);
  /* Receiver thread keep processing sent SSB blocks */
  std::thread receiver(receiver_thread, std::ref(ssb), std::ref(logger), std::ref(config), test_ssb_freq);
  syncer->error_handler = std::bind([&]() { handle_syncer_exit(syncer, sender, receiver); });
  syncer->start(0);
  syncer->wait_thread_finish();
  sender.join();
  receiver.join();
  source->close();
}