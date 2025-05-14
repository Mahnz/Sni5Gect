#include "shadower/hdr/constants.h"
#include "shadower/hdr/source.h"
#include "srsran/radio/radio.h"

class UHDSource final : public Source
{
public:
  /* Initialize the radio object and apply the configurations */
  UHDSource(double             srate_hz,
            double             sdr_srate_hz,
            double             dl_freq,
            double             ul_freq,
            double             rx_gain,
            double             tx_gain,
            uint32_t           nof_channels,
            const std::string& device_args) :
    nof_channels(nof_channels)
  {
    /* Initialize srsran rf multi */
    if (srsran_rf_open_multi(&rf, (char*)device_args.c_str(), nof_channels) != 0) {
      throw std::runtime_error("Failed to open radio");
    }

    /* setup the rf interface */
    set_tx_srate(sdr_srate_hz);
    set_rx_srate(sdr_srate_hz);
    set_rx_freq(dl_freq);
    set_tx_freq(ul_freq);
    set_rx_gain(rx_gain);
    set_tx_gain(tx_gain);
  }

  bool is_sdr() const override { return true; }

  int send(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t& ts, uint32_t slot = 0) override
  {
    std::lock_guard<std::mutex> lock(mutex);
    try {
      int samples_sent = srsran_rf_send_multi(&rf, (void**)buffer, nof_samples, false, true, false);
      return samples_sent;
    } catch (const std::exception& e) {
      return -1;
    }
  }

  int recv(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t* ts) override
  {
    /* Start the rx stream */
    if (!running.load()) {
      srsran_rf_start_rx_stream(&rf, false);
      running.store(true);
    }
    try {
      int samples_recv =
          srsran_rf_recv_with_time_multi(&rf, (void**)buffer, nof_samples, true, &ts->full_secs, &ts->frac_secs);
      if (samples_recv == SRSRAN_ERROR) {
        return -1;
      }
      return samples_recv;
    } catch (const std::exception& e) {
      return -1;
    }
  }

  void close() override { srsran_rf_close(&rf); }
  void set_tx_gain(double gain) override
  {
    for (uint32_t i = 0; i < nof_channels; i++) {
      srsran_rf_set_tx_gain_ch(&rf, i, gain);
    }
  }
  void set_rx_gain(double gain) override
  {
    for (uint32_t i = 0; i < nof_channels; i++) {
      srsran_rf_set_rx_gain_ch(&rf, i, gain);
    }
  }
  void set_tx_srate(double sample_rate) override { srsran_rf_set_tx_srate(&rf, sample_rate); }
  void set_rx_srate(double sample_rate) override { srsran_rf_set_rx_srate(&rf, sample_rate); }
  void set_tx_freq(double freq) override
  {
    for (uint32_t i = 0; i < nof_channels; i++) {
      srsran_rf_set_tx_freq(&rf, i, freq);
    }
  }
  void set_rx_freq(double freq) override
  {
    for (uint32_t i = 0; i < nof_channels; i++) {
      srsran_rf_set_rx_freq(&rf, i, freq);
    }
  }

private:
  srsran_rf_t       rf;
  std::mutex        mutex;
  uint32_t          nof_channels;
  std::atomic<bool> running{false};
};

extern "C" {
__attribute__((visibility("default"))) Source* create_source(ShadowerConfig& config)
{
  return new UHDSource(config.sample_rate,
                       config.source_srate,
                       config.dl_freq,
                       config.ul_freq,
                       config.rx_gain,
                       config.tx_gain,
                       config.nof_channels,
                       config.source_params);
}
}
