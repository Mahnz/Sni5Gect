#ifndef SSB_CUDA_H
#define SSB_CUDA_H
#include "srsran/phy/sync/ssb.h"
#include "srsran/srslog/srslog.h"
#include <cufft.h>

class SSBCuda
{
public:
  SSBCuda(double                      srate_,
          double                      dl_freq_,
          double                      ssb_freq_,
          srsran_subcarrier_spacing_t scs_,
          srsran_ssb_pattern_t        pattern_,
          srsran_duplex_mode_t        duplex_mode_);
  ~SSBCuda();
  bool init(uint32_t N_id_2_);
  void cleanup();
  int  ssb_pss_find_cuda(cf_t* in, uint32_t nof_samples, uint32_t* found_delay);

  int ssb_run_sync_find(cf_t*                          buffer,
                        uint32_t                       N_id,
                        srsran_csi_trs_measurements_t* measurements,
                        srsran_pbch_msg_nr_t*          pbch_msg_nr);
  // bool ssb_run_sync_track(cf_t* buffer);

private:
  srslog::basic_logger&       logger = srslog::fetch_basic_logger("SSBCuda", false);
  srsran_ssb_t                ssb    = {};
  uint32_t                    N_id_2;
  double                      srate;
  double                      dl_freq;
  double                      ssb_freq;
  srsran_subcarrier_spacing_t scs;
  srsran_ssb_pattern_t        pattern;
  srsran_duplex_mode_t        duplex_mode;

  uint32_t total_len;    // ssb_size from last slot
  int      round;        // Total round of correlation
  uint32_t last_len = 0; // last slot length

  cufftHandle   fft_plan  = {};
  cufftHandle   ifft_plan = {};
  cudaStream_t  stream;               // CUDA stream for asynchronous data transfer
  cufftComplex* h_pin_time = nullptr; // Pin host time domain buffer
  cufftComplex* d_time     = nullptr; // Device time domain buffer
  cufftComplex* d_freq     = nullptr; // Device frequency domain buffer
  cufftComplex* d_corr     = nullptr; // Device correlation buffer
  cufftComplex* d_pss_seq  = nullptr; // Device PSS sequence buffer
  float*        d_corr_mag = nullptr; // Device correlation magnitude buffer

  int    compareBlocksPerGrid;
  float *d_block_max_vals, *h_block_max_vals;
  int *  d_block_max_idxs, *h_block_max_idxs;
  void   find_max(float* d_data, int size, float* max_val, int* max_idx);
};
#endif // SSB_CUDA_H