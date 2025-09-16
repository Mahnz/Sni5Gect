#include "shadower/hdr/constants.h"
#include "srsran/config.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <liquid/liquid.h>
#include <vector>
float       input_srate  = 184.32e6;
float       output_srate = 122.88e6;
std::string input_file   = "shadower/test/data/test.fc32";
int         main(int argc, char* argv[])
{
  // Check if the input file exists
  std::ifstream in(input_file, std::ios::binary);
  if (!in.is_open()) {
    std::cout << "[ERROR] Failed to open input file" << std::endl;
    return 1;
  }
  std::cout << "Input file opened successfully!" << std::endl;

  float             resample_rate = output_srate / input_srate;
  uint32_t          sf_len_in     = input_srate * 1e-3;
  uint32_t          sf_len_out    = output_srate * 1e-3;
  std::vector<cf_t> input_buffer(sf_len_in);
  std::vector<cf_t> output_buffer(sf_len_out);
  in.read(reinterpret_cast<char*>(input_buffer.data()), sf_len_in * sizeof(cf_t));

  // Create a liquid resampler object
  msresamp_crcf resampler = msresamp_crcf_create(resample_rate, TARGET_STOPBAND_SUPPRESSION);
  uint32_t      num_output_samples;
  auto          start = std::chrono::high_resolution_clock::now();
  for (uint32_t i = 0; i < 1000; i++) {
    msresamp_crcf_execute(resampler,
                          (liquid_float_complex*)input_buffer.data(),
                          input_buffer.size(),
                          (liquid_float_complex*)output_buffer.data(),
                          &num_output_samples);
  }
  auto end      = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  std::cout << "Resampling time: " << duration.count() / 1000.0 << " us" << std::endl;

  // Write the output buffer to the file
  std::string   output_file = "records/resample_output.fc32";
  std::ofstream out(output_file, std::ios::binary);
  out.write(reinterpret_cast<char*>(output_buffer.data()), num_output_samples * sizeof(cf_t));
  msresamp_crcf_destroy(resampler);
}