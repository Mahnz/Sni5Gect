#include <complex>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

std::string inputFile;
std::string outputFile;
float       skip;
float       subframes;
float       srate         = 23.04e6;
size_t      subframe_size = 23040;

void parse_args(int argc, char* argv[])
{
  int opt;

  while ((opt = getopt(argc, argv, "ioknsh")) != -1) {
    switch (opt) {
      case 'i':
        inputFile = argv[optind];
        break;
      case 'o':
        outputFile = argv[optind];
        break;
      case 'k':
        skip = atof(argv[optind]);
        break;
      case 'n':
        subframes = atof(argv[optind]);
        break;
      case 's': {
        printf("Sample rate: %s\n", argv[optind]);
        double sampleRateMHz = atof(argv[optind]);
        srate                = sampleRateMHz * 1e6;
        subframe_size        = srate * 0.001; // 1ms
        break;
      }
      case 'h':
        printf("Usage: %s -i <input_file> -o <output_file> -k <skip> -n <subframes> -s <sample_rate>\n", argv[0]);
        exit(EXIT_SUCCESS);
      default:
        fprintf(stderr, "Unknown option or missing argument.\n");
        exit(EXIT_FAILURE);
    }
  }
}

void copy(std::ifstream& in, std::ofstream& out, long count)
{
  std::complex<float> buffer[count];
  in.read(reinterpret_cast<char*>(buffer), count * sizeof(std::complex<float>));
  if (in.gcount() < static_cast<std::streamsize>(count * sizeof(std::complex<float>))) {
    printf("[ERROR] Reached end of file, copy failed\n");
    exit(1);
  }
  out.write(reinterpret_cast<char*>(buffer), count * sizeof(std::complex<float>));
}

int main(int argc, char* argv[])
{
  // prepare the input stream object
  parse_args(argc, argv);

  std::ifstream in(inputFile, std::ios::binary);
  printf("Input file: %s\n", inputFile.c_str());
  if (!in.is_open()) {
    printf("[ERROR] Failed to open input file: %s\n", inputFile.c_str());
    exit(1);
  } else {
    printf("[INFO] Input file: %s\n", inputFile.c_str());
  }
  // prepare the output stream object
  std::ofstream out(outputFile, std::ios::binary);
  if (!out.is_open()) {
    printf("[ERROR] Failed to open output file: %s\n", outputFile.c_str());
    exit(1);
  }

  // Print the copy information
  long samplesToSkip = subframe_size * skip;
  printf(" * * *  Skiped Sub-Frames: %.3f \tSamples: %ld\n", skip, samplesToSkip);
  long samplesToCopy = subframe_size * subframes;
  printf(" * * *  Copied Sub-Frames: %.3f \tSamples: %ld\n", subframes, samplesToCopy);

  // Skip the samples
  long offset = samplesToSkip * sizeof(std::complex<float>);
  in.seekg(offset);

  long step_size = 4096;
  // Copy the samples
  while (in.good() && samplesToCopy > 0) {
    step_size = std::min(step_size, samplesToCopy);
    copy(in, out, step_size);
    samplesToCopy -= step_size;
  }
  in.close();
  out.close();
  printf("Expected output file size: %f MB\n",
         static_cast<float>(samplesToCopy * sizeof(std::complex<float>) / 1024 / 1024));
  return 0;
}