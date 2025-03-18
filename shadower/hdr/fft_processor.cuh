#ifndef FFT_PROCESSOR_H
#define FFT_PROCESSOR_H
#include "srsran/srsran.h"
#include <complex>
#include <cuda.h>
#include <cuda_runtime_api.h>
#include <cufft.h>
#include <vector>

void launch_gpu_vec_sc_prod_ccc(cufftComplex* d_signal, cufftComplex* d_phase_list, int fft_size, int symbols_per_slot);

class FFTProcessor
{
public:
  FFTProcessor(double sample_rate_, srsran_subcarrier_spacing_t scs_, uint32_t num_prbs_, double center_freq);
  ~FFTProcessor()
  {
    if (d_signal) {
      cudaFree(d_signal);
    }
    if (plan) {
      cufftDestroy(plan);
    }
  }
  void process_samples(cf_t* buffer, cf_t* ofdm_symbols, uint32_t slot_idx);

  uint32_t fft_size; // FFT size
  uint32_t nof_sc;   // Number of subcarriers

private:
  srsran_subcarrier_spacing_t scs;
  uint32_t                    half_fft;
  uint32_t                    half_subc;
  uint32_t                    scs_khz;
  double                      sample_rate;
  uint32_t                    two_pow_numerology;                          // 2^scs
  uint32_t                    slots_per_subframe;                          // Number of slots per subframe
  uint32_t                    symbols_per_slot = SRSRAN_NSYMB_PER_SLOT_NR; // Number of symbols per slot
  uint32_t                    symbols_per_subframe;                        // Number of symbols per subframe

  double ofdm_units;    // OFDM symbol duration in units of Tc
  double cp_units;      // Normal cyclic prefix duration in units of Tc
  double long_cp_units; // Long cyclic prefix duration in units of Tc

  double ofdm_duration;      // OFDM symbol duration in seconds
  double normal_cp_duration; // Normal CP duration in seconds
  double long_cp_duration;   // Long CP duration in seconds

  uint32_t ofdm_length;    // Number of samples in an OFDM symbol
  uint32_t cp_length;      // Number of samples in a normal CP
  uint32_t long_cp_length; // Number of samples in a long CP

  uint32_t sf_len;

  std::vector<uint32_t>             cp_length_list;
  std::vector<std::complex<float> > phase_compensation_conj_list;
  cufftComplex*                     phase_compensation_list_gpu;

  /* Components used to do FFT using cuda */
  cufftHandle   plan = {};
  cufftComplex* d_signal;        // Allocate GPU memory
  cufftComplex* h_pinned_buffer; // Pin memory for faster data transfer
  cudaStream_t  stream;          // CUDA stream for asynchronous data transfer
};
#endif // FFT_PROCESSOR_H