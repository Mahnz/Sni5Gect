#include "shadower/hdr/constants.h"
#include "shadower/hdr/fft_processor.cuh"
#include <chrono>
#include <cmath>

// Define CUDA kernel
__global__ void
gpu_vec_sc_prod_ccc(cufftComplex* d_signal, cufftComplex* d_phase_list, int fft_size, int symbols_per_slot)
{
  int idx        = threadIdx.x + blockIdx.x * blockDim.x;
  int symbol_idx = blockIdx.y;

  if (idx < fft_size) {
    int          index = symbol_idx * fft_size + idx;
    cufftComplex phase = d_phase_list[symbol_idx];
    cufftComplex result;
    result.x        = d_signal[index].x * phase.x - d_signal[index].y * phase.y;
    result.y        = d_signal[index].x * phase.y + d_signal[index].y * phase.x;
    d_signal[index] = result;
  }
}

__global__ void
gpu_vec_prod_ccc(cufftComplex* d_signal, cufftComplex* d_window_offset_buffer, int fft_size, int symbols_per_slot)
{
  int idx        = threadIdx.x + blockIdx.x * blockDim.x;
  int symbol_idx = blockIdx.y;
  if (idx < fft_size) {
    int          index      = symbol_idx * fft_size + idx;
    cufftComplex win_offset = d_window_offset_buffer[idx];
    cufftComplex result;
    result.x        = d_signal[index].x * win_offset.x - d_signal[index].y * win_offset.y;
    result.y        = d_signal[index].x * win_offset.y + d_signal[index].y * win_offset.x;
    d_signal[index] = result;
  }
}

void launch_gpu_vec_prod_ccc(cufftComplex* d_signal,
                             cufftComplex* d_window_offset_buffer,
                             int           fft_size,
                             int           symbols_per_slot)
{
  dim3 threadsPerBlock(256);
  dim3 numBlocks((fft_size + threadsPerBlock.x - 1) / threadsPerBlock.x, symbols_per_slot);
  // clang-format off
  gpu_vec_prod_ccc<<<numBlocks, threadsPerBlock>>>(d_signal, d_window_offset_buffer, fft_size, symbols_per_slot);
  // clang-format on
}

// Function to launch the kernel
void launch_gpu_vec_sc_prod_ccc(cufftComplex* d_signal, cufftComplex* d_phase_list, int fft_size, int symbols_per_slot)
{
  dim3 threadsPerBlock(256);
  dim3 numBlocks((fft_size + threadsPerBlock.x - 1) / threadsPerBlock.x, symbols_per_slot);
  // clang-format off
  gpu_vec_sc_prod_ccc<<<numBlocks, threadsPerBlock>>>(d_signal, d_phase_list, fft_size, symbols_per_slot);
  // clang-format on
}

FFTProcessor::FFTProcessor(double                      sample_rate_,
                           double                      center_freq_,
                           srsran_subcarrier_spacing_t scs_,
                           srsran_ofdm_t*              fft_) :
  sample_rate(sample_rate_), scs(scs_), fft(fft_), center_freq(center_freq_)
{
  fft_size        = fft->cfg.symbol_sz;
  half_fft        = fft_size / 2;
  nof_re          = fft->nof_re;
  half_re         = nof_re / 2;
  slot_sz         = fft->slot_sz;
  window_offset_n = fft->window_offset_n;

  uint32_t two_pow_numerology   = 1 << scs;
  uint32_t symbols_per_subframe = nof_symbols * two_pow_numerology;
  slot_per_subframe             = two_pow_numerology;

  /* Calculate the duration of OFDM symbol */
  double   ofdm_duration = 2048.0 * K * 1.0 / two_pow_numerology * Tc;
  uint32_t ofdm_len      = std::floor(ofdm_duration * sample_rate);

  /* Calculate the duration of normal cyclic prefix */
  double cp_duration = 144.0 * K * 1.0 / two_pow_numerology * Tc;
  cp_normal_len      = std::floor(cp_duration * sample_rate);

  /* Calcuate the duration of long cyclic prefix */
  double cp_long_duration = (144.0 + 16.0) * K * 1.0 / two_pow_numerology * Tc;
  cp_long_len             = std::floor(cp_long_duration * sample_rate);

  /* Initialize the cyclic prefix */
  cp_length_list.resize(symbols_per_subframe, cp_normal_len);

  /* Long CP list for 0 and 7 * 2^(miu - 1) */
  cp_length_list[0]                      = cp_long_len;
  cp_length_list[7 * two_pow_numerology] = cp_long_len;

  uint32_t             count = 0;
  std::complex<double> I(0, 1);
  /* Initialize phase compensation */
  phase_compensation.resize(symbols_per_subframe);
  for (uint32_t l = 0; l < symbols_per_subframe; l++) {
    uint32_t cp_length = cp_length_list[l];
    count += cp_length;
    double phase_rad      = -2.0 * M_PI * center_freq * (double)count / sample_rate;
    phase_compensation[l] = std::conj(std::exp(I * phase_rad));
    count += ofdm_len;
  }

  /* Allocate Pinned Buffer for GPU */
  cudaMallocHost((void**)&h_pinned_buffer, fft->sf_sz * sizeof(cufftComplex));
  /* Allocate input/output buffer for FFT */
  cudaMalloc((void**)&d_signal, fft->sf_sz * sizeof(cufftComplex));
  /* Initialize FFT Plan */
  int n[1]     = {(int)fft_size};
  int embed[1] = {1};
  int istride  = fft_size + cp_normal_len;
  int ostide   = fft_size;
  cufftPlanMany(&plan, 1, n, embed, 1, istride, embed, 1, ostide, CUFFT_C2C, nof_symbols);
  /* Allocated memory for phase compensation on GPU */
  cudaMalloc((void**)&d_phase_compensation, symbols_per_subframe * sizeof(cufftComplex));
  /* Allocate memory for window offset buffer */
  cudaMalloc((void**)&d_window_offset_buffer, fft_size * sizeof(cufftComplex));
  /* Copy the phase compensation list to GPU */
  cudaMemcpy(d_phase_compensation,
             phase_compensation.data(),
             symbols_per_subframe * sizeof(cufftComplex),
             cudaMemcpyHostToDevice);

  /* Copy the window offset buffer to GPU */
  if (window_offset_n) {
    cudaMemcpy(
        d_window_offset_buffer, fft->window_offset_buffer, fft_size * sizeof(cufftComplex), cudaMemcpyHostToDevice);
  }
  /* Initialize stream */
  cudaStreamCreate(&stream);
  cufftSetStream(plan, stream);
}

/* Process the samples from a slot at a time */
void FFTProcessor::to_ofdm(cf_t* buffer, cf_t* ofdm_symbols, uint32_t slot_idx)
{
  /* Copy the to GPU pinned buffer */
  memcpy(h_pinned_buffer, buffer, slot_sz * sizeof(cufftComplex));

  /* Asynchronous transfer to GPU */
  cudaMemcpyAsync(d_signal, h_pinned_buffer, slot_sz * sizeof(cufftComplex), cudaMemcpyHostToDevice, stream);

  // Run fft
  cufftExecC2C(plan, d_signal + cp_long_len - window_offset_n, d_signal, CUFFT_FORWARD);

  // Apply frequency domain window offset
  if (window_offset_n) {
    launch_gpu_vec_prod_ccc(d_signal, d_window_offset_buffer, fft_size, nof_symbols);
  }

  // Apply phase compensation
  launch_gpu_vec_sc_prod_ccc(d_signal, d_phase_compensation, fft_size, nof_symbols);

  // Copy result back asynchronously
  cudaMemcpyAsync(
      h_pinned_buffer, d_signal, nof_symbols * fft_size * sizeof(cufftComplex), cudaMemcpyDeviceToHost, stream);

  // Wait for all operations to complete
  cudaStreamSynchronize(stream);

  // Copy final output back to host
  for (uint32_t i = 0; i < nof_symbols; i++) {
    // Copy the result to OFDM symbols
    memcpy(ofdm_symbols + i * nof_re, h_pinned_buffer + i * fft_size + fft_size - half_re, half_re * sizeof(cf_t));
    memcpy(ofdm_symbols + i * nof_re + half_re, h_pinned_buffer + i * fft_size, half_re * sizeof(cf_t));
  }
}