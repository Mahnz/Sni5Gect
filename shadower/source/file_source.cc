#include "shadower/source/source.h"
#include "shadower/utils/utils.h"
#include "srsran/phy/common/timestamp.h"
#include <thread>

class FileSource final : public Source
{
public:
  FileSource(std::vector<std::string> filenames, uint32_t num_channels, double sample_rate) :
    srate(sample_rate), nof_channels(num_channels)
  {
    for (uint32_t i = 0; i < num_channels; i++) {
      std::string filename = filenames[i];
      if (filename.empty()) {
        throw std::runtime_error("Error opening file, filename is empty");
      }
      std::ifstream ifile(filename, std::ios::binary);
      if (!ifile.is_open()) {
        throw std::runtime_error("Error opening file");
      }
      ifiles.push_back(std::move(ifile));
      printf("[INFO] Using source file: %s\n", filename.c_str());
    }
    timestamp_prev = {0, 0};
  }

  bool is_sdr() const override { return false; }

  /* Fake send write the samples to send into file */
  int send(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t& ts, uint32_t slot = 0) override
  {
    for (uint32_t i = 0; i < nof_channels; i++) {
      char filename[256];
      sprintf(filename, "tx_slot_ch_%u_%u", i, slot);
      write_record_to_file(buffer[i], nof_samples, filename, "records");
    }
    return nof_samples;
  }

  /* Read the IQ samples from the file, and proceed the timestamp with number of samples / sample rate */
  int recv(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t* ts) override
  {
    for (uint32_t i = 0; i < nof_channels; i++) {
      if (ifiles[i].eof()) {
        return -1;
      }
      ifiles[i].read(reinterpret_cast<char*>(buffer[i]), nof_samples * sizeof(cf_t));
      if (ifiles[i].eof()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return -1;
      }
    }
    srsran_timestamp_add(&timestamp_prev, 0, nof_samples / srate);
    srsran_timestamp_copy(ts, &timestamp_prev);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return nof_samples;
  }

  void close() override
  {
    for (uint32_t i = 0; i < nof_channels; i++) {
      std::ifstream& ifile = ifiles[i];
      if (ifile.is_open()) {
        ifile.close();
      }
    }
  }
  void set_tx_gain(double gain) override {};
  void set_rx_gain(double gain) override {};
  void set_tx_srate(double sample_rate) override {};
  void set_rx_srate(double sample_rate) override {};
  void set_tx_freq(double freq) override {};
  void set_rx_freq(double freq) override {};

private:
  std::vector<std::ifstream> ifiles;
  double                     srate;
  uint32_t                   nof_channels;
  srsran_timestamp_t         timestamp_prev{};
};

extern "C" {
__attribute__((visibility("default"))) Source* create_source(ShadowerConfig& config)
{
  std::vector<std::string> file_names;
  file_names.reserve(config.nof_channels);
  std::stringstream ss(config.source_params);
  std::string       token;
  while (std::getline(ss, token, ',')) {
    if (!token.empty()) {
      file_names.push_back(token);
    }
  }
  return new FileSource(file_names, config.nof_channels, config.sample_rate);
}
}