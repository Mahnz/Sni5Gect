#ifndef FFT_PROCESSOR_H
#define FFT_PROCESSOR_H
#include "srsran/phy/dft/ofdm.h"
#include "srsran/srsran.h"
#include <complex>
#include <cuda.h>
#include <cuda_runtime_api.h>
#include <cufft.h>
#include <vector>

void launch_gpu_vec_sc_prod_ccc(cufftComplex* d_signal, cufftComplex* d_phase_list, int fft_size, int symbols_per_slot);

void launch_gpu_vec_prod_ccc(cufftComplex* d_signal,
                             cufftComplex* d_window_offset_buffer,
                             int           fft_size,
                             int           symbols_per_slot);

class FFTProcessor
{
public:
  FFTProcessor(double sample_rate_, double center_freq_hz, srsran_subcarrier_spacing_t scs_, srsran_ofdm_t* fft_);
  ~FFTProcessor()
  {
    if (d_signal) {
      cudaFree(d_signal);
    }
    if (h_pinned_buffer) {
      cudaFree(h_pinned_buffer);
    }
    if (d_phase_compensation) {
      cudaFree(d_phase_compensation);
    }
    if (d_window_offset_buffer) {
      cudaFree(d_window_offset_buffer);
    }
    if (plan) {
      cufftDestroy(*plan);
    }
    if (stream) {
      cudaStreamDestroy(*stream);
    }
  }

  /* Convert IQ samples to OFDM symbols */
  void to_ofdm(cf_t* buffer, cf_t* ofdm_symbols, uint32_t slot_idx);

  /* Update the phase compensation list */
  void set_phase_compensation(double center_freq);

  uint32_t fft_size; // FFT size
  uint32_t nof_re;   // Number of Resource element

private:
  srsran_ofdm_t* fft = nullptr; // FFT from srsran
  uint32_t       half_fft;      // Half of FFT size
  uint32_t       half_re;
  uint32_t       slot_sz;                                // Slot size
  uint32_t       slot_per_subframe;                      // Number of slots with in a subframe
  uint32_t       symbols_per_subframe;                   // Number of symbols with in a subframe
  float          norm;                                   // Normalization
  float          rx_window_offset;                       // DFT window offset
  uint32_t       nof_symbols = SRSRAN_NSYMB_PER_SLOT_NR; // Number of symbols per slot
  uint32_t       window_offset_n;
  double         sample_rate;           // Sample rate
  double         center_freq;           // Center frequency
  double         phase_compensation_hz; // Carrier frequency in Hz for phase compensation
  uint32_t       cp_normal_len;
  uint32_t       cp_long_len;
  uint32_t       ofdm_len;

  srsran_subcarrier_spacing_t       scs; // Subcarrier spacing for carrier
  std::vector<uint32_t>             cp_length_list;
  std::vector<std::complex<float> > phase_compensation;   // Phase compensation
  std::vector<std::complex<float> > window_offset_buffer; // Frequency domain window offset

  /* Components used to do FFT using cuda */
  cufftHandle*  plan                   = nullptr;
  cufftComplex* d_signal               = nullptr; // Allocate GPU memory
  cufftComplex* h_pinned_buffer        = nullptr; // Pin memory for faster data transfer
  cudaStream_t* stream                 = nullptr; // CUDA stream for asynchronous data transfer
  cufftComplex* d_phase_compensation   = nullptr; // Phase compensation stored in GPU
  cufftComplex* d_window_offset_buffer = nullptr; // Slides DFT window a fraction of cyclic prefix
};
#endif // FFT_PROCESSOR_H