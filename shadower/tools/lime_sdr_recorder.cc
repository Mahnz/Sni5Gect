#include "limesuiteng/limesuiteng.hpp"
#include "srsran/config.h"
#include <csignal>
#include <fstream>
#include <iostream>
#include <string>

using namespace lime;
double         center_freq   = 2550.15e6;
double         sample_rate   = 23.04e6;
double         subframe_time = 1e-3;
double         gain          = 61;
std::string    output_file   = "output.fc32";
int            num_frames    = 1200000;
static uint8_t chipIndex     = 0;

static LogLevel logverbosity = LogLevel::Debug;
static void     LogCallback(LogLevel level, const std::string& msg)
{
  if (level > logverbosity)
    return;
  std::cout << msg << std::endl;
}

bool stopProgram(false);
void sigIntHandler(int)
{
  std::cout << "Stopping program..." << std::endl;
  stopProgram = true;
}

int main(int argc, char* argv[])
{
  if (argc > 1) {
    double centerFreqMHz = atof(argv[1]);
    center_freq          = centerFreqMHz * 1e6;
  }
  if (argc > 2) {
    num_frames = atoi(argv[2]);
  }
  if (argc > 3) {
    output_file = argv[3];
  }
  if (argc > 4) {
    // fourth argument is sample rate in MHz
    double sampleRateMHz = atof(argv[4]);
    sample_rate          = sampleRateMHz * 1e6;
  }

  uint32_t sf_len = sample_rate * subframe_time;

  registerLogHandler(LogCallback);
  auto handles = DeviceRegistry::enumerate();
  if (handles.size() == 0) {
    std::cout << "No LimeSDR devices found" << std::endl;
    return -1;
  }

  for (size_t i = 0; i < handles.size(); i++) {
    std::cout << "Device " << i << ": " << handles[i].Serialize() << std::endl;
  }
  std::cout << std::endl;

  SDRDevice* dev = DeviceRegistry::makeDevice(handles.at(0));
  if (dev == nullptr) {
    std::cout << "Failed to create device" << std::endl;
    return -1;
  }
  std::cout << "Connected to device: " << dev->GetDescriptor().name << std::endl;
  dev->SetMessageLogCallback(LogCallback);
  dev->Init();

  const auto& chipDescriptor = dev->GetDescriptor().rfSOC[chipIndex];
  std::cout << "Chip descriptor: " << chipDescriptor.name << std::endl;

  int               rxPath        = -1;
  const std::string rxAntennaName = "LNAH";
  if (!rxAntennaName.empty()) {
    for (size_t j = 0; j < chipDescriptor.pathNames.at(TRXDir::Rx).size(); j++) {
      std::cout << "Path " << j << ": " << chipDescriptor.pathNames.at(TRXDir::Rx).at(j) << std::endl;
      if (rxAntennaName == chipDescriptor.pathNames.at(TRXDir::Rx).at(j)) {
        rxPath = j;
        break;
      }
    }
  }

  if (rxPath < 0) {
    DeviceRegistry::freeDevice(dev);
    return -1;
  }
  std::cout << "Using antenna " << chipDescriptor.pathNames.at(TRXDir::Rx).at(rxPath) << std::endl;

  SDRConfig config;
  config.channel[0].rx.enabled            = true;
  config.channel[0].rx.centerFrequency    = center_freq;
  config.channel[0].rx.sampleRate         = sample_rate;
  config.channel[0].rx.oversample         = 2;
  config.channel[0].rx.lpf                = 0;
  config.channel[0].rx.path               = rxPath;
  config.channel[0].rx.calibrate          = CalibrationFlag::NONE;
  config.channel[0].rx.testSignal.enabled = false;
  config.channel[0].rx.gain.emplace(lime::eGainTypes::GENERIC, 50);

  config.channel[0].tx.enabled            = false;
  config.channel[0].tx.centerFrequency    = center_freq;
  config.channel[0].tx.sampleRate         = sample_rate;
  config.channel[0].tx.oversample         = 2;
  config.channel[0].tx.lpf                = 0;
  config.channel[0].tx.path               = 0;
  config.channel[0].tx.testSignal.enabled = false;

  dev->Configure(config, chipIndex);

  StreamConfig streamCfg;
  streamCfg.channels[TRXDir::Rx] = {0};
  streamCfg.format               = DataFormat::F32;
  streamCfg.linkFormat           = DataFormat::I12;

  signal(SIGINT, sigIntHandler);

  complex32f_t**            rxSamples = new complex32f_t*[2];
  std::vector<complex32f_t> buffer(sf_len);
  rxSamples[0] = (complex32f_t*)buffer.data();
  rxSamples[1] = new complex32f_t[sf_len];

  // complex16_t**            rxSamples = new complex16_t*[2];
  // std::vector<complex16_t> buffer(sf_len);
  // rxSamples[0] = (complex16_t*)buffer.data();
  // rxSamples[1] = new complex16_t[sf_len];

  std::ofstream out(output_file, std::ios::binary);
  if (!out.is_open()) {
    std::cout << "Failed to open output file" << std::endl;
    return -1;
  }

  StreamMeta                rxMeta{};
  uint32_t                  subframe_count = 0;
  std::unique_ptr<RFStream> stream         = dev->StreamCreate(streamCfg, chipIndex);
  stream->Start();
  std::cout << "Streaming started" << std::endl;
  while (!stopProgram) {
    uint32_t samplesRead = stream->StreamRx(rxSamples, sf_len, &rxMeta);
    if (samplesRead == 0)
      continue;
    out.write(reinterpret_cast<char*>(buffer.data()), samplesRead * sizeof(complex32f_t));
    // out.write(reinterpret_cast<char*>(buffer.data()), samplesRead * sizeof(complex16_t));
    if (subframe_count++ % 100 == 0) {
      printf(".");
      fflush(stdout);
    }
  }

  stream.reset();
  DeviceRegistry::freeDevice(dev);
}