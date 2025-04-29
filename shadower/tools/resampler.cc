#include "shadower/hdr/constants.h"
#include "srsran/config.h"
#include <fstream>
#include <iostream>
#include <liquid/liquid.h>
#include <vector>
float input_srate  = 46.08e6;
float output_srate = 23.04e6;

int main(int argc, char* argv[])
{
  if (argc < 5) {
    printf("Usage: resampler_test <input file> <input srate> <output file> <output srate>\n");
    return 1;
  }
  std::string input_file  = argv[1];
  input_srate             = atof(argv[2]) * 1e6;
  std::string output_file = argv[3];
  output_srate            = atof(argv[4]) * 1e6;

  // Check if the input sample rate is greater than the output sample rate
  if (input_srate <= output_srate) {
    std::cout << "[ERROR] Input sample rate must be greater than output sample rate!" << std::endl;
    return 1;
  }

  // Check if the input file exists
  std::ifstream in(input_file, std::ios::binary);
  if (!in.is_open()) {
    std::cout << "[ERROR] Failed to open input file" << std::endl;
    return 1;
  }
  std::cout << "Input file opened successfully!" << std::endl;

  // Prepare the output stream object
  std::ofstream out(output_file, std::ios::binary);
  if (!out.is_open()) {
    std::cout << "[ERROR] Failed to open output file" << std::endl;
    return 1;
  }
  std::cout << "Output file opened successfully!" << std::endl;

  uint32_t          sf_len_in  = input_srate * 10e-3;
  uint32_t          sf_len_out = output_srate * 10e-3;
  std::vector<cf_t> input_buffer(sf_len_in);
  std::vector<cf_t> output_buffer(sf_len_out);

  // Create a resampler object
  float         resample_rate = output_srate / input_srate;
  msresamp_crcf resampler     = msresamp_crcf_create(resample_rate, TARGET_STOPBAND_SUPPRESSION);

  while (in.good()) {
    // Read the input buffer
    in.read(reinterpret_cast<char*>(input_buffer.data()), sf_len_in * sizeof(cf_t));
    if (in.gcount() != sf_len_in * sizeof(cf_t)) {
      break; // End of file or read error
    }

    uint32_t num_output_samples;
    msresamp_crcf_execute(resampler,
                          (liquid_float_complex*)input_buffer.data(),
                          input_buffer.size(),
                          (liquid_float_complex*)output_buffer.data(),
                          &num_output_samples);

    // Write the output buffer to the file
    out.write(reinterpret_cast<char*>(output_buffer.data()), num_output_samples * sizeof(cf_t));

    // Destroy the resampler object
  }
  msresamp_crcf_destroy(resampler);
}