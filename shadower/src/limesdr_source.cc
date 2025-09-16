#include "limesuiteng/limesuiteng.hpp"
#include "shadower/hdr/constants.h"
#include "shadower/hdr/ring_buffer.h"
#include "shadower/hdr/source.h"
#include <atomic>
#include <vector>

class LimeSDRSource final : public Source
{
public:
  LimeSDRSource(std::string device_args,
                double      srate_,
                double      rx_freq,
                double      tx_freq,
                double      rx_gain,
                double      tx_gain) :
    srate(srate_)
  {
    logger.set_level(srslog::basic_levels::info);
    /* Register the logger call back */
    parse_parameters(device_args);
    lime::registerLogHandler(
        std::bind(&LimeSDRSource::log_callback, this, std::placeholders::_1, std::placeholders::_2));
    /* Check devices available */
    auto handles = lime::DeviceRegistry::enumerate();
    if (handles.size() == 0) {
      throw std::runtime_error("No LimeSDR devices found");
    }

    for (size_t i = 0; i < handles.size(); i++) {
      printf(GREEN "[LimeSDR]" RESET "Device: %zu: %s\n", i, handles[i].Serialize().c_str());
    }

    if (params.find("serial") != params.end()) {
      /* Select device with same serial */
      for (auto handle : handles) {
        if (handle.serial == params["serial"]) {
          printf(GREEN "[LimeSDR]" RESET "Using device with serial: %s\n", params["serial"].c_str());
          device = lime::DeviceRegistry::makeDevice(handle);
        }
      }
    } else {
      /* Select the first available handle */
      printf(GREEN "[LimeSDR]" RESET "Using first available device\n");
      device = lime::DeviceRegistry::makeDevice(handles.at(0));
    }

    if (device == nullptr) {
      throw std::runtime_error("Failed to create LimeSDR device, no matching device found");
    }
    printf(GREEN "[LimeSDR]" RESET "Connected to device: %s\n", device->GetDescriptor().name.c_str());
    device->SetMessageLogCallback(
        std::bind(&LimeSDRSource::log_callback, this, std::placeholders::_1, std::placeholders::_2));
    device->Init();

    /* Specify the chip descriptor */
    for (size_t i = 0; i < device->GetDescriptor().rfSOC.size(); i++) {
      printf(GREEN "[LimeSDR]" RESET "Chip %zu: %s\n", i, device->GetDescriptor().rfSOC[i].name.c_str());
    }

    const lime::RFSOCDescriptor& chipDescriptor = device->GetDescriptor().rfSOC[chipIndex];
    printf(GREEN "[LimeSDR]" RESET "Using Chip Descriptor: %s\n", chipDescriptor.name.c_str());

    /* Specify RX antenna path */
    if (!rx_antenna_name.empty()) {
      for (size_t j = 0; j < chipDescriptor.pathNames.at(lime::TRXDir::Rx).size(); j++) {
        std::string path_name = chipDescriptor.pathNames.at(lime::TRXDir::Rx).at(j);
        printf(GREEN "[LimeSDR]" RESET "RX Path %zu: %s\n", j, path_name.c_str());
        if (rx_antenna_name == path_name) {
          rx_path = j;
          printf(GREEN "[LimeSDR]" RESET "Using RX Path: %s\n", path_name.c_str());
          break;
        }
      }
    }
    if (rx_path < 0) {
      lime::DeviceRegistry::freeDevice(device);
      throw std::runtime_error("Invalid RX antenna path");
    }

    /* Specify TX antenna path */
    if (!tx_antenna_name.empty()) {
      for (size_t j = 0; j < chipDescriptor.pathNames.at(lime::TRXDir::Tx).size(); j++) {
        std::string path_name = chipDescriptor.pathNames.at(lime::TRXDir::Tx).at(j);
        printf(GREEN "[LimeSDR]" RESET "TX Path %zu: %s\n", j, path_name.c_str());
        if (tx_antenna_name == path_name) {
          tx_path = j;
          printf(GREEN "[LimeSDR]" RESET "Using TX Path: %s\n", path_name.c_str());
<<<<<<< HEAD
          break;
=======
>>>>>>> 98c31033ed8bcf6d3d9782acadb05df5aae3336b
        }
      }
    }
    if (tx_path < 0) {
      lime::DeviceRegistry::freeDevice(device);
      throw std::runtime_error("Invalid TX antenna path");
    }

    lime::Range<double> rx_range = chipDescriptor.gainRange.at(lime::TRXDir::Rx).at(lime::eGainTypes::GENERIC);
    lime::Range<double> tx_range = chipDescriptor.gainRange.at(lime::TRXDir::Tx).at(lime::eGainTypes::GENERIC);
    printf(GREEN "[LimeSDR]" RESET "Chip Gain Range RX: %f - %f\n", rx_range.min, rx_range.max);
    printf(GREEN "[LimeSDR]" RESET "Chip Gain Range TX: %f - %f\n", tx_range.min, tx_range.max);
    printf(GREEN "[LimeSDR]" RESET "Frequency Range: %f - %f\n",
           chipDescriptor.frequencyRange.min,
           chipDescriptor.frequencyRange.max);
    printf(GREEN "[LimeSDR]" RESET "Sample Rate Range: %f - %f\n",
           chipDescriptor.samplingRateRange.min,
           chipDescriptor.samplingRateRange.max);
<<<<<<< HEAD

    /* Specify the configuration */
    config.channel[0].rx.enabled            = true;
    config.channel[0].rx.centerFrequency    = rx_freq + frequency_correction;
    config.channel[0].rx.sampleRate         = srate;
    config.channel[0].rx.oversample         = 2;
    config.channel[0].rx.lpf                = 0;
    config.channel[0].rx.path               = rx_path;
    config.channel[0].rx.calibrate          = calibration_flag;
    config.channel[0].rx.testSignal.enabled = false;
    config.channel[0].rx.gain.emplace(lime::eGainTypes::GENERIC, rx_gain);

    config.channel[0].tx.enabled            = true;
    config.channel[0].tx.centerFrequency    = tx_freq + frequency_correction;
    config.channel[0].tx.sampleRate         = srate;
    config.channel[0].tx.oversample         = 2;
    config.channel[0].tx.lpf                = 0;
    config.channel[0].tx.path               = tx_path;
    config.channel[0].tx.calibrate          = calibration_flag;
    config.channel[0].tx.testSignal.enabled = false;
    config.channel[0].tx.gain.emplace(lime::eGainTypes::GENERIC, tx_gain);
=======
    printf(GREEN "[LimeSDR]" RESET "Number of channels: %u\n", chipDescriptor.channelCount);

    /* Specify the configuration */
    for (uint8_t ch = 0; ch < channelCount; ch++) {
      config.channel[ch].rx.enabled            = true;
      config.channel[ch].rx.centerFrequency    = rx_freq + frequency_correction;
      config.channel[ch].rx.sampleRate         = srate;
      config.channel[ch].rx.oversample         = 2;
      config.channel[ch].rx.lpf                = 0;
      config.channel[ch].rx.path               = rx_path;
      config.channel[ch].rx.calibrate          = calibration_flag;
      config.channel[ch].rx.testSignal.enabled = false;
      config.channel[ch].rx.gain.emplace(lime::eGainTypes::GENERIC, rx_gain);

      if (tx_enabled) {
        config.channel[ch].tx.enabled            = true;
        config.channel[ch].tx.centerFrequency    = tx_freq + frequency_correction;
        config.channel[ch].tx.sampleRate         = srate;
        config.channel[ch].tx.oversample         = 2;
        config.channel[ch].tx.lpf                = 0;
        config.channel[ch].tx.path               = tx_path;
        config.channel[ch].tx.calibrate          = calibration_flag;
        config.channel[ch].tx.testSignal.enabled = false;
        config.channel[ch].tx.gain.emplace(lime::eGainTypes::GENERIC, tx_gain);
      }
    }
>>>>>>> 98c31033ed8bcf6d3d9782acadb05df5aae3336b

    if (device->Configure(config, chipIndex) != lime::OpStatus::Success) {
      throw std::runtime_error("Failed to configure device");
    }

    /* Stream configuration */
<<<<<<< HEAD
    streamCfg.channels[lime::TRXDir::Rx] = {0};
    streamCfg.channels[lime::TRXDir::Tx] = {0};
    streamCfg.format                     = lime::DataFormat::F32;
    streamCfg.linkFormat                 = lime::DataFormat::I12;
=======
    streamCfg.channels.at(lime::TRXDir::Rx).clear();
    streamCfg.channels.at(lime::TRXDir::Tx).clear();
    for (uint8_t ch = 0; ch < channelCount; ch++) {
      streamCfg.channels[lime::TRXDir::Rx].push_back(ch);
      if (tx_enabled) {
        streamCfg.channels[lime::TRXDir::Tx].push_back(ch);
      }
    }

    streamCfg.format     = lime::DataFormat::F32;
    streamCfg.linkFormat = lime::DataFormat::I12;
>>>>>>> 98c31033ed8bcf6d3d9782acadb05df5aae3336b
  }

  bool is_sdr() const override { return true; }

  int send(cf_t* samples, uint32_t length, srsran_timestamp_t& tx_time, uint32_t slot = 0) override
  {
    lime::StreamMeta txMeta{};
    txMeta.waitForTimestamp     = true;
    txMeta.flushPartialPacket   = true;
    txMeta.timestamp            = srsran_timestamp_uint64(&tx_time, srate);
    lime::complex32f_t* dest[2] = {0};
    dest[0]                     = (lime::complex32f_t*)samples;
    if (tx_time.full_secs < 0 || isnan(tx_time.frac_secs)) {
      return SRSRAN_ERROR;
    }
    uint32_t samples_sent = stream->StreamTx(dest, length, &txMeta);
    return (int)samples_sent;
  }

  /* Rx function for getting the IQ samples */
  int receive(cf_t* buffer, uint32_t nof_samples, srsran_timestamp_t* ts) override
  {
    if (stream == nullptr || !streamRunning) {
      stream = device->StreamCreate(streamCfg, chipIndex);
      stream->Start();
      streamRunning.store(true);
    }
    lime::StreamMeta    rxMeta{};
    lime::complex32f_t* dest[SRSRAN_MAX_PORTS] = {0};
    dest[0]                                    = (lime::complex32f_t*)buffer;
    uint32_t samples_received                  = stream->StreamRx(dest, nof_samples, &rxMeta);
    double   total_secs                        = (double)rxMeta.timestamp / srate;
    ts->full_secs                              = static_cast<time_t>(total_secs);
    ts->frac_secs                              = double(total_secs - ts->full_secs);
    return (int)samples_received;
  }

  void close() override
  {
    stream->Stop();
    stream.reset();
    streamRunning.store(false);
    lime::DeviceRegistry::freeDevice(device);
    device = nullptr;
  }

  void set_tx_gain(double gain) override
  {
    for (const int ch : streamCfg.channels.at(lime::TRXDir::Tx)) {
      device->SetGain(chipIndex, lime::TRXDir::Tx, ch, lime::eGainTypes::GENERIC, gain);
    }
  }

  void set_rx_gain(double gain) override
  {
    for (const int ch : streamCfg.channels.at(lime::TRXDir::Rx)) {
      device->SetGain(chipIndex, lime::TRXDir::Rx, ch, lime::eGainTypes::GENERIC, gain);
    }
  }

  void set_tx_srate(double sample_rate) override
  {
    for (const int ch : streamCfg.channels.at(lime::TRXDir::Tx)) {
      device->SetSampleRate(chipIndex, lime::TRXDir::Tx, ch, sample_rate, 1);
    }
  }

  void set_rx_srate(double sample_rate) override
  {
    for (const int ch : streamCfg.channels.at(lime::TRXDir::Rx)) {
      device->SetSampleRate(chipIndex, lime::TRXDir::Rx, ch, sample_rate, 1);
    }
  }

  void set_tx_freq(double freq) override
  {
    for (const int ch : streamCfg.channels.at(lime::TRXDir::Tx)) {
      device->SetFrequency(chipIndex, lime::TRXDir::Tx, ch, freq);
    }
  };

  void set_rx_freq(double freq) override
  {
    for (const int ch : streamCfg.channels.at(lime::TRXDir::Rx)) {
      device->SetFrequency(chipIndex, lime::TRXDir::Rx, ch, freq);
    }
  };

private:
  double                srate;
  srslog::basic_logger& logger = srslog::fetch_basic_logger("LimeSDR");
  lime::LogLevel        log_level;
  std::string           port;
<<<<<<< HEAD
  int                   chipIndex = 0;
  int                   rx_path   = -1;
=======
  int                   chipIndex    = 0;
  uint8_t               channelCount = 0;
  bool                  tx_enabled   = true;
  int                   rx_path      = -1;
>>>>>>> 98c31033ed8bcf6d3d9782acadb05df5aae3336b
  std::string           rx_antenna_name;
  int                   tx_path = -1;
  std::string           tx_antenna_name;
  lime::SDRConfig       config    = {};
  lime::StreamConfig    streamCfg = {};
  std::atomic<bool>     streamRunning{false};
  float                 frequency_correction = 34e3;

  std::unique_ptr<lime::RFStream> stream = nullptr;
  lime::SDRDevice*                device = nullptr;

  lime::CalibrationFlag calibration_flag = lime::CalibrationFlag::NONE;

  std::unordered_map<std::string, std::string> params;

  void parse_parameters(const std::string& line)
  {
    std::stringstream ss(line);
    std::string       pair;
    while (std::getline(ss, pair, ',')) {
      auto sep = pair.find(':');
      if (sep != std::string::npos) {
        std::string key   = pair.substr(0, sep);
        std::string value = pair.substr(sep + 1);
        if (!value.empty() && value.front() == '"' && value.back() == '"') {
          value = value.substr(1, value.size() - 2); // remove quotes
        }
        params[key] = value;
      }
    }

    if (params.find("logLevel") != params.end()) {
      int level = std::stoi(params["logLevel"]);
      if (level > 3) {
        logger.set_level(srslog::basic_levels::debug);
        log_level = lime::LogLevel::Debug;
      } else {
        logger.set_level(srslog::basic_levels::info);
        log_level = lime::LogLevel::Info;
      }
    }

<<<<<<< HEAD
    if (params.find("dev0_chipIndex") != params.end()) {
      chipIndex = std::stoi(params["dev0_chipIndex"]);
    }

    if (params.find("dev0_rx_path") != params.end()) {
      rx_antenna_name = params["dev0_rx_path"];
    }

    if (params.find("dev0_tx_path") != params.end()) {
      tx_antenna_name = params["dev0_tx_path"];
    }

    if (params.find("dev0_calibration") != params.end()) {
      std::string calibration = params["dev0_calibration"];
=======
    if (params.find("chipIndex") != params.end()) {
      chipIndex = std::stoi(params["chipIndex"]);
    }

    if (params.find("channels") != params.end()) {
      channelCount = std::stoi(params["channels"]);
    }

    if (params.find("tx_enabled") != params.end()) {
      tx_enabled = std::stoi(params["tx_enabled"]);
    }

    if (params.find("rx_path") != params.end()) {
      rx_antenna_name = params["rx_path"];
    }

    if (params.find("tx_path") != params.end()) {
      tx_antenna_name = params["tx_path"];
    }

    if (params.find("calibration") != params.end()) {
      std::string calibration = params["calibration"];
>>>>>>> 98c31033ed8bcf6d3d9782acadb05df5aae3336b
      if (calibration == "none") {
        calibration_flag = lime::CalibrationFlag::NONE;
      } else if (calibration == "dciq") {
        calibration_flag = lime::CalibrationFlag::DCIQ;
      } else if (calibration == "filter") {
        calibration_flag = lime::CalibrationFlag::FILTER;
      } else {
        throw std::invalid_argument("Invalid device calibration");
      }
    }

    if (params.find("freq_corr") != params.end()) {
      frequency_correction = std::stof(params["freq_corr"]);
      printf(GREEN "[LimeSDR]" RESET "Using Frequency Correction: %f\n", frequency_correction);
    } else {
      printf(GREEN "[LimeSDR]" RESET "Using default Frequency Correction: %f\n", frequency_correction);
    }
  }

<<<<<<< HEAD
  // void log_callback(lime::LogLevel level, const std::string& msg) { printf(GREEN "[LimeSDR]" RESET "%s",
  // msg.c_str()); }
=======
>>>>>>> 98c31033ed8bcf6d3d9782acadb05df5aae3336b
  void log_callback(lime::LogLevel level, const std::string& msg)
  {
    if (level <= log_level) {
      printf(GREEN "[LimeSDR]" RESET " %s\n", msg.c_str());
    }
  };
};

extern "C" {
__attribute__((visibility("default"))) Source* create_source(ShadowerConfig& config)
{
  return new LimeSDRSource(
      config.source_params, config.sample_rate, config.dl_freq, config.ul_freq, config.rx_gain, config.tx_gain);
}
}