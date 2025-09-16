#include "shadower/hdr/msg_helper.h"
#include "srsran/asn1/rrc_nr.h"
#include "srsran/mac/mac_sch_pdu_nr.h"
#include <iomanip>
#include <sstream>

/* Put the nas message in to dl_dcch_msg */
asn1::rrc_nr::dl_dcch_msg_s pack_nas_to_dl_dcch(const std::string& nas_msg)
{
  asn1::rrc_nr::dl_dcch_msg_s       dl_dcch_msg;
  asn1::rrc_nr::dl_info_transfer_s& dl_information_transfer = dl_dcch_msg.msg.set_c1().set_dl_info_transfer();
  dl_information_transfer.rrc_transaction_id                = 0;
  asn1::rrc_nr::dl_info_transfer_ies_s& dl_information_transfer_ies =
      dl_information_transfer.crit_exts.set_dl_info_transfer();
  dl_information_transfer_ies.ded_nas_msg.from_string(nas_msg);
  return dl_dcch_msg;
}

/* Put the dl_dcch msg into rrc nr and encode it */
bool pack_dl_dcch_to_rrc_nr(srsran::unique_byte_buffer_t& buffer, const asn1::rrc_nr::dl_dcch_msg_s& dl_dcch_msg)
{
  asn1::bit_ref bref{buffer->msg + buffer->N_bytes, buffer->get_tailroom()};
  if (dl_dcch_msg.pack(bref) != asn1::SRSASN_SUCCESS) {
    printf("Error packing dl_info_transfer\n");
    return false;
  }
  buffer->N_bytes += bref.distance_bytes();
  return true;
}

void pack_rrc_nr_to_rlc_nr(uint8_t*                      rrc_nr_msg,
                           uint32_t                      rrc_nr_len,
                           uint16_t                      am_sn,
                           uint16_t                      pdcp_sn,
                           uint8_t*                      rrc_mac,
                           srsran::unique_byte_buffer_t& output)
{
  /* AM header */
  uint8_t am_header[2] = {0};
  am_header[0]         = ((am_sn >> 8) & 0xf) | 0xc0;
  am_header[1]         = am_sn & 0xff;
  output->append_bytes(am_header, sizeof(am_header));

  /* PDCP header */
  uint8_t pdcp_header[2] = {0};
  pdcp_header[0]         = (pdcp_sn >> 8) & 0xff;
  pdcp_header[1]         = pdcp_sn & 0xff;
  output->append_bytes(pdcp_header, sizeof(pdcp_header));

  /* Put the rrc-nr message into rlc-nr */
  output->append_bytes(rrc_nr_msg, rrc_nr_len);

  /* Append the rrc-nr mac to the end */
  output->append_bytes(rrc_mac, 4);
}

/* Put rrc nr message into rlc nr*/
void pack_rlc_nr_to_mac_nr(uint8_t*               rlc_nr_msg,
                           uint32_t               rlc_nr_len,
                           uint16_t               ack_sn,
                           srsran::byte_buffer_t& output,
                           uint32_t               pdu_len)
{
  srsran::mac_sch_pdu_nr mac_pdu;
  mac_pdu.init_tx(&output, pdu_len);

  if (ack_sn > 0) {
    /* add an ack to the message */
    uint8_t rlc_ack_pdu[3] = {0};
    rlc_ack_pdu[0]         = 0x0;
    rlc_ack_pdu[1]         = ack_sn & 0xff;
    rlc_ack_pdu[2]         = 0;
    mac_pdu.add_sdu(1, rlc_ack_pdu, 3);
  }

  /* Add the rlc-nr buffer to mac-nr*/
  mac_pdu.add_sdu(1, rlc_nr_msg, rlc_nr_len);
  mac_pdu.pack();
}