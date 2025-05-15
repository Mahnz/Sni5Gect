#include "shadower/utils/utils.h"
#include <chrono>
#include <fstream>

/* Write the IQ samples to a file so that we can use tools like matlab or spectrogram-py to debug */
void write_record_to_file(cf_t* buffer, uint32_t length, char* name, const std::string& folder)
{
  char filename[256];
  if (name) {
    sprintf(filename, "%s/%s.fc32", folder.c_str(), name);
  } else {
    auto now                     = std::chrono::high_resolution_clock::now();
    auto nanoseconds_since_epoch = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    sprintf(filename, "%s/record_%ld.fc32", folder.c_str(), nanoseconds_since_epoch);
  }
  std::ofstream f(filename, std::ios::binary);
  if (f) {
    f.write(reinterpret_cast<char*>(buffer), length * sizeof(cf_t));
    f.close();
  } else {
    printf("Error opening file: %s\n", filename);
  }
}

