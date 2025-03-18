#include "shadower/hdr/utils.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/sync/ssb.h"
#include "srsran/srslog/srslog.h"
#include "test_variables.h"
#include <fstream>
#if ENABLE_CUDA
#include "shadower/hdr/ssb_cuda.cuh"
#endif // ENABLE_CUDA

#if TEST_TYPE == 1
std::string sample_file = "shadower/test/data/srsran-n78-20MHz/sib.fc32";
#elif TEST_TYPE == 2
std::string sample_file = "shadower/test/data/ssb.fc32";
#elif TEST_TYPE == 3
std::string sample_file = "shadower/test/data/srsran-n78-40MHz/sib.fc32";
#endif // TEST_TYPE
int main()
{
  /* initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::debug);

  /* initialize ssb */
  srsran_ssb_t ssb = {};
  if (!init_ssb(ssb, srate, dl_freq, ssb_freq, scs, pattern, duplex)) {
    logger.error("Failed to initialize SSB");
    return -1;
  }

  /* load samples from file */
  std::vector<cf_t> samples(sf_len);
  if (!load_samples(sample_file, samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", sample_file.c_str());
    return -1;
  }

  /* search for SSB */
  srsran_ssb_search_res_t res = {};
  if (srsran_ssb_search(&ssb, samples.data(), sf_len, &res) < SRSRAN_SUCCESS) {
    logger.error("Error running srsran_ssb_search");
    return -1;
  }
  if (res.measurements.snr_dB < -10.0f || !res.pbch_msg.crc) {
    logger.error("Failed to decode PBCH message");
    return -1;
  }

  /* decode MIB */
  srsran_mib_nr_t mib = {};
  if (srsran_pbch_msg_nr_mib_unpack(&res.pbch_msg, &mib) < SRSRAN_SUCCESS) {
    logger.error("Error running srsran_pbch_msg_nr_mib_unpack");
    return -1;
  }

  /* get SSB index */
  uint32_t sf_idx   = srsran_ssb_candidate_sf_idx(&ssb, res.pbch_msg.ssb_idx, res.pbch_msg.hrf);
  uint32_t slot_idx = mib.sfn * 10 * slots_per_sf + sf_idx;
  logger.info("SF index: %u Slot index: %u", sf_idx, slot_idx);

  /* write MIB to file */
  std::array<char, 512> mib_info_str = {};
  srsran_pbch_msg_nr_mib_info(&mib, mib_info_str.data(), mib_info_str.size());
  std::ofstream mib_raw{mib_config_raw, std::ios::binary};
  mib_raw.write(reinterpret_cast<const char*>(&mib), sizeof(mib));

  logger.info("Found cell: %s", mib_info_str.data());
  logger.info("Cell id: %u", res.N_id);
  logger.info("Offset: %u", res.t_offset);
  logger.info("CFO: %f", res.measurements.cfo_hz);

  /* Measure SSB find time */
  auto                          t_start_ssb_find     = std::chrono::high_resolution_clock::now();
  srsran_csi_trs_measurements_t ssb_find_measurement = {};
  srsran_pbch_msg_nr_t          ssb_find_pbch_msg    = {};
  if (srsran_ssb_find(&ssb, samples.data(), res.N_id, &ssb_find_measurement, &ssb_find_pbch_msg) != SRSRAN_SUCCESS) {
    logger.error("Error running srsran_ssb_find");
    return -1;
  }
  if (!ssb_find_pbch_msg.crc) {
    logger.error("PBCH CRC not match (srsran_ssb_find)");
  } else {
    logger.info("PBCH CRC matched (srsran_ssb_find)");
  }
  auto t_end_ssb_find      = std::chrono::high_resolution_clock::now();
  auto t_duration_ssb_find = std::chrono::duration_cast<std::chrono::microseconds>(t_end_ssb_find - t_start_ssb_find);
  logger.info("srsran_ssb_find: %ld us", t_duration_ssb_find.count());

  /* Measure SSB track time */
  auto                          t_start_ssb_track      = std::chrono::high_resolution_clock::now();
  srsran_csi_trs_measurements_t ssb_track_measurements = {};
  srsran_pbch_msg_nr_t          ssb_track_pbch_msg     = {};
  uint32_t                      half_frame             = 0;
  if (srsran_ssb_track(&ssb,
                       samples.data(),
                       res.N_id,
                       res.pbch_msg.ssb_idx,
                       half_frame,
                       &ssb_track_measurements,
                       &ssb_track_pbch_msg) != SRSRAN_SUCCESS) {
    logger.error("Error running srsran_ssb_track");
    return -1;
  }
  auto t_end_ssb_track = std::chrono::high_resolution_clock::now();
  auto t_duration_ssb_track =
      std::chrono::duration_cast<std::chrono::microseconds>(t_end_ssb_track - t_start_ssb_track);
  logger.info("srsran_ssb_track: %ld us", t_duration_ssb_track.count());
#if ENABLE_CUDA
  SSBCuda ssb_cuda(srate, dl_freq, ssb_freq, scs, pattern, duplex);
  if (!ssb_cuda.init(SRSRAN_NID_2_NR(res.N_id))) {
    logger.error("Failed to initialize SSB CUDA");
    return -1;
  }

  /* SSB cuda run sync find time */
  srsran_csi_trs_measurements_t cuda_measurements = {};
  srsran_pbch_msg_nr_t          cuda_pbch_msg     = {};
  auto                          t_start_cuda      = std::chrono::high_resolution_clock::now();
  ssb_cuda.ssb_run_sync_find(samples.data(), res.N_id, &cuda_measurements, &cuda_pbch_msg);
  auto t_end_cuda      = std::chrono::high_resolution_clock::now();
  auto t_duration_cuda = std::chrono::duration_cast<std::chrono::microseconds>(t_end_cuda - t_start_cuda);
  logger.info("ssb_run_sync_find: %ld us", t_duration_cuda.count());

  srsran_mib_nr_t mib_cuda = {};
  if (cuda_pbch_msg.crc) {
    logger.info("PBCH CRC matched");
  } else {
    logger.error("PBCH CRC not match");
    return -1;
  }
  if (srsran_pbch_msg_nr_mib_unpack(&cuda_pbch_msg, &mib_cuda) < SRSRAN_SUCCESS) {
    logger.error("Error running srsran_pbch_msg_nr_mib_unpack");
    return -1;
  }
  srsran_pbch_msg_nr_mib_info(&mib_cuda, mib_info_str.data(), (uint32_t)mib_info_str.size());
  logger.info(YELLOW "Found cell: %s" RESET, mib_info_str.data());
  ssb_cuda.cleanup();
#endif // ENABLE_CUDA
  return 0;
}
