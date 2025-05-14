#ifndef SOURCE_H
#define SOURCE_H
#include "shadower/hdr/arg_parser.h"
#include "srsran/phy/common/timestamp.h"

class Source
{
public:
  virtual ~Source() = default;
  virtual bool is_sdr() const { return false; }
  virtual int  recv(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t* ts)                    = 0;
  virtual int  send(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t& ts, uint32_t slot = 0) = 0;
  virtual void close()                                                                              = 0;
  virtual void set_tx_gain(double gain)                                                             = 0;
  virtual void set_rx_gain(double gain)                                                             = 0;
  virtual void set_tx_srate(double sample_rate)                                                     = 0;
  virtual void set_rx_srate(double sample_rate)                                                     = 0;
  virtual void set_tx_freq(double freq)                                                             = 0;
  virtual void set_rx_freq(double freq)                                                             = 0;
};

using create_source_t = Source* (*)(ShadowerConfig & config);

/* Function used to load source module */
create_source_t load_source(const std::string filename);
#endif // SOURCE_H