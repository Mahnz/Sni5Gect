#include "shadower/hdr/source.h"
#include "shadower/hdr/utils.h"
#include <liquid/liquid.h>

class FileSource final : public Source
{
public:
  FileSource(const char* file_name, double sample_rate, ShadowerConfig& config) :
    ifile{file_name, std::ifstream::binary}, srate(sample_rate)
  {
    if (config.enable_resampler) {
      if (config.source_srate < srate) {
        throw std::runtime_error("Source sample rate must be higher than the configured sample rate");
      }
      resample_rate    = srate / config.source_srate;
      resampler        = msresamp_crcf_create(resample_rate, TARGET_STOPBAND_SUPPRESSION);
      enable_resampler = true;
    }
    if (!ifile.is_open()) {
      throw std::runtime_error("Error opening file");
    }
    printf("[INFO] Using source file: %s\n", file_name);
    timestamp_prev = {0, 0};
  }

  bool is_sdr() const override { return false; }

  /* Fake send write the samples to send into file */
  int send(cf_t* samples, uint32_t length, srsran_timestamp_t& tx_time, uint32_t slot = 0) override
  {
    char filename[256];
    sprintf(filename, "tx_slot_%u", slot);
    write_record_to_file(samples, length, filename, "records");
    return length;
  }

  /* Read the IQ samples from the file, and proceed the timestamp with number of samples / sample rate */
  int receive(cf_t* buffer, uint32_t nof_samples, srsran_timestamp_t* ts) override
  {
    ifile.read(reinterpret_cast<char*>(buffer), nof_samples * sizeof(cf_t));
    srsran_timestamp_add(&timestamp_prev, 0, nof_samples / srate);
    srsran_timestamp_copy(ts, &timestamp_prev);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (ifile.eof()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      return -1;
    }
    if (enable_resampler) {
      uint32_t num_output_samples;
      msresamp_crcf_execute(
          resampler, (liquid_float_complex*)buffer, nof_samples, (liquid_float_complex*)buffer, &num_output_samples);
      return (int)num_output_samples;
    }
    return nof_samples;
  }

  void close() override
  {
    if (ifile.is_open()) {
      ifile.close();
    }
  }
  void set_tx_gain(double gain) override {};
  void set_rx_gain(double gain) override {};
  void set_tx_srate(double sample_rate) override {};
  void set_rx_srate(double sample_rate) override {};
  void set_tx_freq(double freq) override {};
  void set_rx_freq(double freq) override {};

private:
  std::ifstream      ifile;
  double             srate;
  srsran_timestamp_t timestamp_prev{};

  msresamp_crcf resampler;
  float         resample_rate    = 0.5f; // Example resample rate
  bool          enable_resampler = false;
};

extern "C" {
__attribute__((visibility("default"))) Source* create_source(ShadowerConfig& config)
{
  return new FileSource(config.source_params.c_str(), config.sample_rate, config);
}
}