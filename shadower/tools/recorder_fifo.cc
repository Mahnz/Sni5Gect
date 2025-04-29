#include "shadower/hdr/constants.h"
#include "srsran/phy/utils/vector.h"
#include <atomic>
#include <complex>
#include <condition_variable>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <iostream>
#include <liquid/liquid.h>
#include <mutex>
#include <pthread.h>
#include <queue>
#include <sched.h>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <uhd/usrp/multi_usrp.hpp>
#include <unistd.h>
#include <vector>
#define FIFO_NAME "/tmp/recorder_fifo"

using namespace std;
atomic<bool> stop_flag(false);
typedef struct {
  double        freq;
  double        srate;
  uint32_t      frames;
  msresamp_crcf resampler;
  string        outputFileName;
} sdr_params_t;

const double gain         = 40;   // dB
const double subframeTime = 1e-3; // 1 ms
// string       device_args  = "type=b200";
string device_args = "type=x300,addr=192.168.40.2,sampling_rate=184.32e6,master_clock_rate=184.32e6,send_frame_size="
                     "8000,recv_frame_size=8000,clock=internal";
bool   enable_resampler = false;
double sdr_srate        = 184.32e6;

void* read_from_sdr(void* args)
{
  auto*    sdr_params = (sdr_params_t*)args;
  double   freq       = sdr_params->freq;
  double   srate      = sdr_params->srate;
  uint32_t frames     = sdr_params->frames;

  // create a USRP device
  uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(device_args);
  usrp->set_rx_rate(sdr_srate);
  printf("Using sample rate SDR sample_rate: %f\n", sdr_srate);
  usrp->set_rx_freq(freq);
  usrp->set_rx_gain(gain);
  usrp->set_clock_source("internal");
  const size_t buffer_size = srate * subframeTime;
  // Create a receiver streamer
  uhd::rx_streamer::sptr stream = usrp->get_rx_stream(uhd::stream_args_t("fc32", "sc16"));
  // Create a receive metadata structure
  uhd::rx_metadata_t metadata;
  // Start streaming
  uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
  stream_cmd.num_samps  = 0;
  stream_cmd.stream_now = true;
  stream->issue_stream_cmd(stream_cmd);

  // Open FIFO for writing
  int fifo_fd = open(FIFO_NAME, O_WRONLY);
  int size    = buffer_size * sizeof(complex<float>) * 16384;
  fcntl(fifo_fd, F_SETPIPE_SZ, size);

  if (fifo_fd == -1) {
    cerr << "Failed to open FIFO for writing" << endl;
    stop_flag.store(true);
    return nullptr;
  }

  long subFrameCount = 0;

  std::vector<complex<float> > buffer(buffer_size);
  while (subFrameCount < frames && !stop_flag.load()) {
    try {
      stream->recv(buffer.data(), buffer_size, metadata, 3.0);
      subFrameCount++;

      ssize_t bytes_to_write = buffer_size * sizeof(complex<float>);
      ssize_t bytes_written  = write(fifo_fd, buffer.data(), bytes_to_write);
      if (bytes_written != bytes_to_write) {
        cerr << "Failed to write to FIFO" << endl;
        stop_flag.store(true);
        break;
      }
    } catch (const exception& e) {
      break;
    }
  }
  close(fifo_fd);
  return nullptr;
}

void write_to_file(void* args)
{
  auto*    sdr_params = (sdr_params_t*)args;
  ofstream outfile(sdr_params->outputFileName, ios::binary);
  if (!outfile.is_open()) {
    cerr << "Failed to open output file" << endl;
    stop_flag.store(true);
    return;
  }

  int fifo_fd = open(FIFO_NAME, O_RDONLY);
  if (fifo_fd == -1) {
    cerr << "Failed to open FIFO for reading" << endl;
    stop_flag.store(true);
    return;
  }

  size_t                       buffer_size = sdr_params->srate * subframeTime;
  std::vector<complex<float> > buffer(buffer_size);
  uint32_t                     numSubframes = 0;
  while (!stop_flag.load()) {
    ssize_t bytes_expected = buffer_size * sizeof(std::complex<float>);
    ssize_t bytes_read     = read(fifo_fd, buffer.data(), bytes_expected);
    if (bytes_read == -1) {
      cerr << "Failed to read from FIFO" << endl;
      stop_flag.store(true);
      break;
    }

    if (enable_resampler) {
      uint32_t                     num_output_samples;
      std::vector<complex<float> > output_buffer(buffer_size);
      msresamp_crcf_execute(sdr_params->resampler,
                            (liquid_float_complex*)buffer.data(),
                            buffer.size(),
                            (liquid_float_complex*)output_buffer.data(),
                            &num_output_samples);
      outfile.write(reinterpret_cast<char*>(output_buffer.data()), num_output_samples * sizeof(complex<float>));
    } else {
      outfile.write(reinterpret_cast<char*>(buffer.data()), bytes_read);
    }

    if (numSubframes++ % 100 == 0) {
      printf(".");
      fflush(stdout);
    }
  }
  close(fifo_fd);
  outfile.close();
}

int main(int argc, char* argv[])
{
  double   centerFrequency = 3424.5e6; // 3424.5 MHz
  double   sampleRate      = 46.08e6;  // 46.08 MHz
  string   outputFile      = "output.fc32";
  uint32_t num_frames      = 1200000;

  if (argc > 1) {
    double centerFrequencyMHz = atof(argv[1]);
    centerFrequency           = centerFrequencyMHz * 1e6;
    printf("Using center frequency: %f\n", centerFrequency);
  }

  if (argc > 2) {
    num_frames = atoi(argv[2]);
    printf("Using number of frames: %d\n", num_frames);
  }

  if (argc > 3) {
    outputFile = argv[3];
    printf("Using output file: %s\n", outputFile.c_str());
  }

  if (argc > 4) {
    double sampleRateMHz = atof(argv[4]);
    sampleRate           = sampleRateMHz * 1e6;
    printf("Using sample rate: %f\n", sampleRate);
  }

  if (argc > 5) {
    double sdr_srateMHz = atof(argv[5]);
    sdr_srate           = sdr_srateMHz * 1e6;
    if (sdr_srate < sampleRate) {
      cerr << "SDR sample rate must be greater than or equal to the sample rate" << endl;
      return -1;
    }
    if (sdr_srate != sampleRate) {
      enable_resampler = true;
    }
    printf("Using SDR sample rate: %f\n", sdr_srate);
  } else {
    sdr_srate = sampleRate;
    printf("Using SDR sample rate: %f\n", sdr_srate);
  }

  if (argc > 6) {
    device_args = argv[6];
  }

  float         resample_rate = sampleRate / sdr_srate;
  msresamp_crcf resampler     = msresamp_crcf_create(resample_rate, TARGET_STOPBAND_SUPPRESSION);
  printf("Using sample rate: %f\n", sdr_srate);
  if (enable_resampler) {
    printf("Using resampling rate: %f\n", resample_rate);
  }
  sdr_params_t params = {centerFrequency, sdr_srate, num_frames * 10, resampler, outputFile};

  // Create FIFO
  unlink(FIFO_NAME);
  if (mkfifo(FIFO_NAME, 0666) == -1) {
    cerr << "Failed to create FIFO" << endl;
    return -1;
  }

  if (setpriority(PRIO_PROCESS, 0, -20) != 0) {
    cerr << "Failed to set process priority" << endl;
    return -1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    perror("Fork failed");
    return -1;
  }

  if (pid == 0) {
    write_to_file((void*)&params);
    exit(0);
  } else {
    read_from_sdr((void*)&params);
    wait(nullptr);     // Wait for child process to finish
    unlink(FIFO_NAME); // Remove FIFO
  }
  msresamp_crcf_destroy(resampler);
  return 0;
}
