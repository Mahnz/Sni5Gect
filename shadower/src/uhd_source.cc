#include "shadower/hdr/constants.h"
#include "shadower/hdr/source.h"
#include "srsran/radio/radio.h"
#include <liquid/liquid.h>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/thread.hpp>

class UHDSource final : public Source
{
public:
  /* Initialize the radio object and apply the configurations */
  UHDSource(std::string     device_args,
            double          srate_,
            double          rx_freq,
            double          tx_freq,
            double          rx_gain,
            double          tx_gain,
            ShadowerConfig& config) :
    srate(srate_), enable_resampler(config.enable_resampler)
  {
    if (config.enable_resampler) {
      if (config.source_srate < srate) {
        throw std::runtime_error("Source sample rate must be higher than the configured sample rate");
      }
      resample_rate = srate / config.source_srate;
      resampler     = msresamp_crcf_create(resample_rate, TARGET_STOPBAND_SUPPRESSION);
    }

    if (srsran_rf_open(&rf, (char*)device_args.c_str()) != 0) {
      throw std::runtime_error("Failed to open radio");
    }

    if (enable_resampler) {
      srsran_rf_set_rx_srate(&rf, config.source_srate);
      srsran_rf_set_tx_srate(&rf, config.source_srate);
    } else {
      srsran_rf_set_rx_srate(&rf, srate);
      srsran_rf_set_tx_srate(&rf, srate);
    }
    srsran_rf_set_rx_freq(&rf, 0, rx_freq);
    srsran_rf_set_rx_gain(&rf, rx_gain);

    srsran_rf_set_tx_freq(&rf, 0, tx_freq);
    srsran_rf_set_tx_gain(&rf, tx_gain);
  }

  bool is_sdr() const override { return true; }

  int send(cf_t* samples, uint32_t length, srsran_timestamp_t& tx_time, uint32_t slot = 0) override
  {
    std::lock_guard<std::mutex> lock(mutex);
    try {
      int samples_sent = srsran_rf_send_timed2(&rf, samples, length, tx_time.full_secs, tx_time.frac_secs, true, true);
      return samples_sent;
    } catch (const std::exception& e) {
      return -1;
    }
  }

  int receive(cf_t* buffer, uint32_t nof_samples, srsran_timestamp_t* ts) override
  {
    try {
      int samples_received = srsran_rf_recv_with_time(&rf, buffer, nof_samples, true, &ts->full_secs, &ts->frac_secs);
      if (enable_resampler) {
        uint32_t num_output_samples;
        msresamp_crcf_execute(
            resampler, (liquid_float_complex*)buffer, nof_samples, (liquid_float_complex*)buffer, &num_output_samples);
        return (int)num_output_samples;
      }
      return samples_received;
    } catch (const std::exception& e) {
      return -1;
    }
  }

  void close() override { srsran_rf_close(&rf); }
  void set_srate(double srate_) { srate = srate_; }
  void set_tx_gain(double gain) override { srsran_rf_set_tx_gain(&rf, gain); }
  void set_rx_gain(double gain) override { srsran_rf_set_rx_gain(&rf, gain); }
  void set_tx_srate(double sample_rate) override { srsran_rf_set_tx_srate(&rf, sample_rate); }
  void set_rx_srate(double sample_rate) override { srsran_rf_set_rx_srate(&rf, sample_rate); }
  void set_tx_freq(double freq) override { srsran_rf_set_tx_freq(&rf, 0, freq); }
  void set_rx_freq(double freq) override { srsran_rf_set_rx_freq(&rf, 0, freq); }

private:
  srsran_rf_t   rf{};
  std::mutex    mutex;
  double        srate;
  msresamp_crcf resampler;
  float         resample_rate    = 0.5f; // Example resample rate
  bool          enable_resampler = false;
};

extern "C" {
__attribute__((visibility("default"))) Source* create_source(ShadowerConfig& config)
{
  return new UHDSource(
      config.source_params, config.sample_rate, config.dl_freq, config.ul_freq, config.rx_gain, config.tx_gain, config);
}
}
