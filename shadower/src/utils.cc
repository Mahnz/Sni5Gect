#include "shadower/hdr/utils.h"
#include "shadower/hdr/constants.h"
#include "shadower/hdr/exploit.h"
#include "shadower/hdr/source.h"
#include "srsran/adt/circular_map.h"
#include "srsran/asn1/rrc_nr_utils.h"
#include "srsran/common/pcap.h"
#include "srsran/mac/mac_rar_pdu_nr.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <iomanip>
#include <pthread.h>
#include <sched.h>
#include <sstream>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

#define IOPRIO_CLASS_SHIFT 13
#define IOPRIO_PRIO_VALUE(class, data) (((class) << IOPRIO_CLASS_SHIFT) | data)

enum {
  IOPRIO_CLASS_NONE,
  IOPRIO_CLASS_RT,
  IOPRIO_CLASS_BE,
  IOPRIO_CLASS_IDLE,
};

enum {
  IOPRIO_WHO_PROCESS = 1,
  IOPRIO_WHO_PGRP,
  IOPRIO_WHO_USER,
};

static inline int ioprio_set(int which, int who, int ioprio)
{
  return syscall(SYS_ioprio_set, which, who, ioprio);
}

/* Initialize logger */
srslog::basic_logger& srslog_init(ShadowerConfig* config)
{
  srslog::init();
  srslog::sink* sink        = nullptr;
  sink                      = srslog::create_stdout_sink();
  srslog::log_channel* chan = srslog::create_log_channel("main", *sink);
  srslog::set_default_sink(*sink);
  return srslog::fetch_basic_logger("main", false);
}

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

/* Load the IQ samples from a file */
bool load_samples(const std::string& filename, cf_t* buffer, size_t nsamples)
{
  std::ifstream infile(filename, std::ios::binary);
  if (!infile.is_open()) {
    return false;
  }
  infile.read(reinterpret_cast<char*>(buffer), nsamples * sizeof(cf_t));
  infile.close();
  return true;
}

/* Function used to load exploit module */
create_exploit_t load_exploit(std::string& filename)
{
  /* Open the shared library */
  void* handle = dlopen(filename.c_str(), RTLD_LAZY);
  if (!handle) {
    std::cerr << "Error loading module: " + filename + " - " + dlerror() << std::endl;
    return nullptr;
  }

  /* Load the create_exploit function from the shared library */
  auto create_exploit = reinterpret_cast<create_exploit_t>(dlsym(handle, "create_exploit"));
  if (!create_exploit) {
    std::cerr << "Error loading symbol 'create_exploit' from " + filename + ": " + dlerror() << std::endl;
    dlclose(handle);
    return nullptr;
  }
  return create_exploit;
}

/* Function used to load exploit module */
create_source_t load_source(const std::string filename)
{
  /* Open the shared library */
  void* handle = dlopen(filename.c_str(), RTLD_LAZY);
  if (!handle) {
    std::cerr << "Error loading module: " + filename + " - " + dlerror() << std::endl;
    return nullptr;
  }

  /* Load the create_exploit function from the shared library */
  auto create_source = reinterpret_cast<create_source_t>(dlsym(handle, "create_source"));
  if (!create_source) {
    std::cerr << "Error loading symbol 'create_source' from " + filename + ": " + dlerror() << std::endl;
    dlclose(handle);
    return nullptr;
  }
  return create_source;
}

/* Read binary form configuration dumped structure */
bool read_raw_config(const std::string& filename, uint8_t* buffer, size_t size)
{
  std::ifstream infile(filename, std::ios::binary);
  if (!infile.is_open()) {
    return false;
  }
  infile.read(reinterpret_cast<char*>(buffer), size);
  return true;
}

/* Print the buffer as hex */
std::string vec_to_hex_str(uint8_t* buffer, size_t size)
{
  std::ostringstream oss;
  for (uint32_t i = 0; i < size; i++) {
    oss << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(buffer[i]) << ", ";
  }
  return oss.str();
}

/* Set the thread priority */
void set_thread_priority(std::thread& t, int priority)
{
  pthread_t          native_handle = t.native_handle();
  struct sched_param param{};
  param.sched_priority = priority;
  if (pthread_setschedparam(native_handle, SCHED_FIFO, &param) != 0) {
    std::cerr << "Failed to set thread priority" << std::endl;
  }
}

bool enable_rt_scheduler(uint8_t use_full_time)
{
  // Configure hard limits
  system(("prlimit --rtprio=unlimited:unlimited --pid " + std::to_string(getpid())).c_str());
  system(("prlimit --nice=unlimited:unlimited --pid " + std::to_string(getpid())).c_str());

  // Set schedule priority
  struct sched_param sp;
  int                policy = 0;

  sp.sched_priority     = sched_get_priority_max(SCHED_FIFO);
  pthread_t this_thread = pthread_self();

  int ret = sched_setscheduler(0, SCHED_FIFO, &sp);
  if (ret) {
    puts("Error: sched_setscheduler: Failed to change scheduler to RR");
    return false;
  }

  ret = pthread_getschedparam(this_thread, &policy, &sp);
  if (ret) {
    puts("Error: Couldn't retrieve real-time scheduling parameters");
    return false;
  }

  // LOG2G("Thread priority is ", sp.sched_priority);

  // Allow thread to be cancelable
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

  // Set IO prioriy
  ioprio_set(IOPRIO_WHO_PROCESS, 0, IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, 0));

  if (use_full_time) {
    int fd = ::open("/proc/sys/kernel/sched_rt_runtime_us", O_RDWR);
    if (fd) {
      if (::write(fd, "-1", 2) > 0)
        puts("/proc/sys/kernel/sched_rt_runtime_us = -1");
    }
  }

  return true;
}

/* Add the required UDP header for wdissector */
int add_fake_header(uint8_t*             buffer,
                    uint8_t*             data,
                    uint32_t             len,
                    uint16_t             rnti,
                    uint16_t             frame_number,
                    uint16_t             slot_number,
                    direction_t          direction,
                    srsran_duplex_mode_t duplex_mode)
{
  memccpy(buffer, fake_pcap_header, fake_pcap_header_len, 2048);
  uint8_t* payload = buffer + fake_pcap_header_len;
  memset(payload, 0, 1988);
  int      offset = 0;
  uint16_t tmp16;
  payload[offset++] = (duplex_mode == SRSRAN_DUPLEX_MODE_FDD) ? 1 : 2;
  payload[offset++] = (direction == DL) ? 1 : 0;
  payload[offset++] = 0x3;
  /* RNTI */
  payload[offset++] = MAC_LTE_RNTI_TAG;
  tmp16             = htons(rnti);
  memcpy(payload + offset, &tmp16, 2);
  offset += 2;
  payload[offset++] = 0x07;
  /* system frame number */
  tmp16 = htons(frame_number);
  memcpy(payload + offset, &tmp16, 2);
  offset += 2;
  /* slot number */
  tmp16 = htons(slot_number);
  memcpy(payload + offset, &tmp16, 2);
  offset += 2;
  payload[offset++] = MAC_LTE_PAYLOAD_TAG;
  memcpy(payload + offset, data, len);

  uint16_t udp_payload_length = htons(len + 26);
  memcpy(buffer + 38, &udp_payload_length, 2);
  uint16_t ip_payload_length = htons(udp_payload_length + 20);
  memcpy(buffer + 16, &ip_payload_length, 2);
  return payload + offset + len - buffer;
}

bool set_rar_grant(uint16_t                                        rnti,
                   srsran_rnti_type_t                              rnti_type,
                   uint32_t                                        slot_idx,
                   std::array<uint8_t, SRSRAN_RAR_UL_GRANT_NBITS>& grant,
                   srsran::phy_cfg_nr_t&                           phy_cfg,
                   srsue::nr::state&                               phy_state,
                   srslog::basic_logger&                           logger)
{
  srsran_dci_msg_nr_t dci_msg = {};
  dci_msg.ctx.format          = srsran_dci_format_nr_rar; /* MAC RAR grant shall be unpacked as DCI 0_0 format */
  dci_msg.ctx.rnti_type       = rnti_type;
  dci_msg.ctx.ss_type         = srsran_search_space_type_rar; /* This indicates it is a MAC RAR */
  dci_msg.ctx.rnti            = rnti;
  dci_msg.nof_bits            = SRSRAN_RAR_UL_GRANT_NBITS;
  srsran_vec_u8_copy(dci_msg.payload, grant.data(), SRSRAN_RAR_UL_GRANT_NBITS);
  srsran_dci_ul_nr_t dci_ul = {};
  if (srsran_dci_nr_ul_unpack(NULL, &dci_msg, &dci_ul) < SRSRAN_SUCCESS) {
    logger.error("Couldn't unpack UL grant");
    return false;
  }
  if (logger.debug.enabled()) {
    std::array<char, 512> str{};
    srsran_dci_nr_t       dci = {};
    srsran_dci_ul_nr_to_str(&dci, &dci_ul, str.data(), str.size());
    logger.debug("Setting RAR Grant: %s", str.data());
  }
  srsran_slot_cfg_t slot_cfg = {.idx = slot_idx + 1};
  phy_state.set_ul_pending_grant(phy_cfg, slot_cfg, dci_ul);
  return true;
}

/* Decode SIB1 bytes to asn1 structure */
bool parse_to_sib1(uint8_t* data, uint32_t len, asn1::rrc_nr::sib1_s& sib1)
{
  asn1::rrc_nr::bcch_dl_sch_msg_s dlsch_msg;
  asn1::cbit_ref                  dlsch_bref(data, len);
  asn1::SRSASN_CODE               err = dlsch_msg.unpack(dlsch_bref);
  if (err != asn1::SRSASN_SUCCESS ||
      dlsch_msg.msg.type().value != asn1::rrc_nr::bcch_dl_sch_msg_type_c::types_opts::c1) {
    std::cerr << "Error unpacking BCCH-BCH message\n";
    return false;
  }
  sib1 = dlsch_msg.msg.c1().sib_type1();
  return true;
}

/* Decode dl_ccch_msg_s bytes to asn1 structure */
bool parse_to_dl_ccch_msg(uint8_t* data, uint32_t len, asn1::rrc_nr::dl_ccch_msg_s& dl_ccch_msg)
{
  asn1::cbit_ref    bref(data, len);
  asn1::SRSASN_CODE err = dl_ccch_msg.unpack(bref);
  if (err != asn1::SRSASN_SUCCESS || dl_ccch_msg.msg.type().value != asn1::rrc_nr::dl_ccch_msg_type_c::types_opts::c1) {
    std::cerr << "Error unpacking DL-CCCH message\n";
    return false;
  }
  return true;
}

/* extract cell_group struct from rrc_setup */
bool extract_cell_group_cfg(asn1::rrc_nr::dl_ccch_msg_s& dl_ccch_msg, asn1::rrc_nr::cell_group_cfg_s& cell_group)
{
  asn1::rrc_nr::rrc_setup_s& rrc_setup_msg = dl_ccch_msg.msg.c1().rrc_setup();
  asn1::cbit_ref             bref_cg(rrc_setup_msg.crit_exts.rrc_setup().master_cell_group.data(),
                         rrc_setup_msg.crit_exts.rrc_setup().master_cell_group.size());
  if (cell_group.unpack(bref_cg) != asn1::SRSASN_SUCCESS) {
    printf("Could not unpack master cell group config.\n");
    return false;
  }
  return true;
}

/* extract cell_group struct from rrc_setup */
std::shared_ptr<asn1::rrc_nr::cell_group_cfg_s> extract_cell_group_cfg(asn1::rrc_nr::rrc_setup_s& rrc_setup_msg)
{
  asn1::cbit_ref                 bref_cg(rrc_setup_msg.crit_exts.rrc_setup().master_cell_group.data(),
                         rrc_setup_msg.crit_exts.rrc_setup().master_cell_group.size());
  asn1::rrc_nr::cell_group_cfg_s cell_group;
  if (cell_group.unpack(bref_cg) != asn1::SRSASN_SUCCESS) {
    printf("Could not unpack master cell group config.\n");
    return nullptr;
  }
  return std::make_shared<asn1::rrc_nr::cell_group_cfg_s>(cell_group);
}

/* Initialize phy state object */
void init_phy_state(srsue::nr::state& phy_state, uint32_t nof_prb)
{
  /* physical state to help track grants */
  phy_state.stack                 = nullptr;
  phy_state.args.nof_carriers     = 1;
  phy_state.args.dl.nof_max_prb   = nof_prb;
  phy_state.args.dl.pdsch.max_prb = nof_prb;
  phy_state.args.ul.nof_max_prb   = nof_prb;
  phy_state.args.ul.pusch.max_prb = nof_prb;
}

/* Calculate the RA-rnti from SIB1 configuration */
std::vector<uint16_t> get_ra_rnti_list(asn1::rrc_nr::sib1_s sib1, ShadowerConfig& config)
{
  std::vector<uint16_t>    ra_rnti_list;
  std::vector<uint16_t>    t_idx_list;
  const prach_nr_config_t* prach_cfg_nr;
  uint16_t                 ul_carrier_id;
  uint32_t                 num_ra_rnti = 0;

  if (!sib1.serving_cell_cfg_common_present || !sib1.serving_cell_cfg_common.ul_cfg_common_present ||
      !sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.rach_cfg_common_present) {
    return ra_rnti_list;
  }

  /* Get the prach configuration index */
  uint16_t prach_cfg_idx =
      sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.rach_cfg_common.setup().rach_cfg_generic.prach_cfg_idx;

  /* Retrieve the PRACH config using cfg idx */
  if (config.duplex_mode == SRSRAN_DUPLEX_MODE_TDD) {
    prach_cfg_nr  = srsran_prach_nr_get_cfg_fr1_unpaired(prach_cfg_idx);
    ul_carrier_id = 0;
  } else if (config.duplex_mode == SRSRAN_DUPLEX_MODE_FDD) {
    prach_cfg_nr  = srsran_prach_nr_get_cfg_fr1_paired(prach_cfg_idx);
    ul_carrier_id = 0;
  } else {
    return ra_rnti_list;
  }

  /* Get the number of RA-RNTI */
  num_ra_rnti = prach_cfg_nr->nof_subframe_number;
  t_idx_list.resize(prach_cfg_nr->nof_subframe_number);
  ra_rnti_list.resize(prach_cfg_nr->nof_subframe_number);

  /* Get the list of t_idx */
  if (sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.rach_cfg_common.setup().prach_root_seq_idx.type() == 0) {
    for (uint32_t t_idx_id = 0; t_idx_id < prach_cfg_nr->nof_subframe_number; t_idx_id++) {
      t_idx_list[t_idx_id] = prach_cfg_nr->subframe_number[t_idx_id];
    }
  } else if (sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.rach_cfg_common.setup().prach_root_seq_idx.type() ==
             1) {
    uint32_t slots_per_sf = SRSRAN_NSLOTS_PER_SF_NR(config.scs_common);
    t_idx_list.resize(prach_cfg_nr->nof_subframe_number * slots_per_sf);
    ra_rnti_list.resize(prach_cfg_nr->nof_subframe_number * slots_per_sf);
    num_ra_rnti = prach_cfg_nr->nof_subframe_number * slots_per_sf;
    for (uint32_t t_idx_id = 0; t_idx_id < prach_cfg_nr->nof_subframe_number; t_idx_id++) {
      for (uint32_t slot_idx = 0; slot_idx < slots_per_sf; slot_idx++) {
        t_idx_list[t_idx_id * slots_per_sf + slot_idx] =
            prach_cfg_nr->subframe_number[t_idx_id] * slots_per_sf + slot_idx;
      }
    }
  } else {
    return ra_rnti_list;
  }

  /*  TS - 38.321: 5.1.3 Random Access Preamble transmission
    RA-RNTI = 1 + s_id + 14 × t_id + 14 × 80 × f_id + 14 × 80 × 8 × ul_carrier_id.
    s_id is the index of the first OFDM symbol of the PRACH occasion (0 ≤ s_id < 14)
    t_id is the index of the first slot of the PRACH occasion in a system frame (0 ≤ t_id < 80)
    subcarrier spacing to determine t_id is based on the value of μ specified in clause 5.3.2 in TS 38.211
    f_id is the index of the PRACH occasion in the frequency domain (0 ≤ f_id < 8)
    ul_carrier_id is the UL carrier used for Random Access Preamble transmission (0 for NUL carrier, and 1 for SUL
    carrier)
    */
  for (uint32_t i = 0; i < num_ra_rnti; i++) {
    uint16_t s_id = prach_cfg_nr->starting_symbol;
    uint16_t t_id = t_idx_list[i];
    uint16_t f_id = 0;

    uint16_t ra_rnti = 1 + s_id + 14 * t_id + 14 * 80 * f_id + 14 * 80 * 8 * ul_carrier_id;
    ra_rnti_list[i]  = ra_rnti;
  }
  return ra_rnti_list;
}

/* Helper function to initialize ssb */
bool init_ssb(srsran_ssb_t&               ssb,
              double                      srate,
              double                      dl_freq,
              double                      ssb_freq,
              srsran_subcarrier_spacing_t scs,
              srsran_ssb_pattern_t        pattern,
              srsran_duplex_mode_t        duplex_mode)
{
  srsran_ssb_args_t ssb_args = {};
  ssb_args.max_srate_hz      = srate;
  ssb_args.min_scs           = scs;
  ssb_args.enable_search     = true;
  ssb_args.enable_measure    = true;
  ssb_args.enable_decode     = true;
  if (srsran_ssb_init(&ssb, &ssb_args) != 0) {
    printf("Error initialize ssb\n");
    return false;
  }
  srsran_ssb_cfg_t ssb_cfg = {};
  ssb_cfg.srate_hz         = srate;
  ssb_cfg.center_freq_hz   = dl_freq;
  ssb_cfg.ssb_freq_hz      = ssb_freq;
  ssb_cfg.scs              = scs;
  ssb_cfg.pattern          = pattern;
  ssb_cfg.duplex_mode      = duplex_mode;
  ssb_cfg.periodicity_ms   = 10;
  if (srsran_ssb_set_cfg(&ssb, &ssb_cfg) < SRSRAN_SUCCESS) {
    printf("Error set srsran_ssb_set_cfg\n");
    return false;
  }
  return true;
}

/* Initialize phy cfg from shadower configuration */
void init_phy_cfg(srsran::phy_cfg_nr_t& phy_cfg, ShadowerConfig& config)
{
  phy_cfg.carrier.dl_center_frequency_hz = config.dl_freq;
  phy_cfg.carrier.ul_center_frequency_hz = config.ul_freq;
  phy_cfg.carrier.ssb_center_freq_hz     = config.ssb_freq;
  phy_cfg.carrier.offset_to_carrier      = 0;
  phy_cfg.carrier.scs                    = config.scs_common;
  phy_cfg.carrier.nof_prb                = config.nof_prb;
  phy_cfg.carrier.max_mimo_layers        = 1;
  phy_cfg.duplex.mode                    = config.duplex_mode;
  phy_cfg.ssb.periodicity_ms             = 10;
  phy_cfg.ssb.position_in_burst[0]       = true;
  phy_cfg.ssb.scs                        = config.scs_ssb;
  phy_cfg.ssb.pattern                    = config.ssb_pattern;
}

/* Print and compare two different buffers */
void compare_two_buffers(uint8_t* buffer1, uint32_t len1, uint8_t* buffer2, uint32_t len2)
{
  std::ostringstream oss1;
  std::ostringstream oss2;
  uint32_t           len = std::min(len1, len2);
  for (uint32_t i = 0; i < len; i++) {
    oss1 << "0x" << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(buffer1[i]) << ", ";
    if (buffer1[i] != buffer2[i]) {
      oss2 << RED "0x" << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(buffer2[i]) << RESET ", ";
    } else {
      oss2 << "0x" << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(buffer2[i]) << ", ";
    }
  }
  for (uint32_t i = len; i < len1; i++) {
    oss1 << GREEN "0x" << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(buffer1[i]) << RESET ", ";
  }
  for (uint32_t i = len; i < len2; i++) {
    oss2 << GREEN "0x" << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(buffer2[i]) << RESET ", ";
  }
  printf("%s\n", oss1.str().c_str());
  printf("%s\n", oss2.str().c_str());
}

/* Turn a buffer to hex string */
std::string buffer_to_hex_string(uint8_t* buffer, uint32_t len)
{
  std::ostringstream oss;
  for (uint32_t i = 0; i < len; i++) {
    if (i == len - 1) {
      oss << "0x" << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(buffer[i]);
    } else {
      oss << "0x" << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(buffer[i]) << ", ";
    }
  }
  return oss.str();
}

/* Parse slot index from filename */
uint32_t parse_slot_idx_from_filename(const std::string& filename)
{
  size_t last_under_score = filename.find_last_of('_');
  size_t last_dot         = filename.find_last_of('.');
  if (last_under_score == std::string::npos || last_dot == std::string::npos) {
    return 0;
  }
  std::string slot_idx_str = filename.substr(last_under_score + 1, last_dot - last_under_score - 1);
  return std::stoi(slot_idx_str);
}

/* Parse the command line arguments for test */
test_args_t parse_test_args(int argc, char* argv[])
{
  test_args_t args = {};
  int         opt;
  int         option_index = 0;

  /* Define long options */
  static struct option long_options[] = {{"dci", required_argument, nullptr, 0}, /* Long option for --dci */
                                         {nullptr, 0, nullptr, 0}};

  while ((opt = getopt_long(argc, argv, "f:s:r:h:d:l:o:", long_options, &option_index)) != -1) {
    switch (opt) {
      case 'f':
        args.sample_filename = optarg;
        args.slot_idx        = parse_slot_idx_from_filename(args.sample_filename);
        break;
      case 's':
        args.slot_idx = std::stoi(optarg);
        break;
      case 'r':
        args.rnti = std::stoi(optarg);
        break;
      case 'h':
        args.half = std::stoi(optarg);
        break;
      case 'd':
        args.delay = std::stoi(optarg);
        break;
      case 'l':
        args.last_sample_filename = optarg;
        break;
      case 'o':
        args.cfo = std::stof(optarg);
        break;
      case 0:
        if (std::string(long_options[option_index].name) == "dci") {
          args.dci_sample_filename = optarg;
        }
        break;
      default:
        break;
    }
  }
  return args;
}

/* Load mib configuration from file and apply to phy cfg */
bool configure_phy_cfg_from_mib(srsran::phy_cfg_nr_t& phy_cfg, std::string& filename, uint32_t ncellid)
{
  srsran_mib_nr_t mib = {};
  if (!read_raw_config(filename, (uint8_t*)&mib, sizeof(srsran_mib_nr_t))) {
    return false;
  }
  if (!update_phy_cfg_from_mib(phy_cfg, mib, ncellid)) {
    return false;
  }
  return true;
}

/* Load SIB1 configuration from file and apply to phy cfg */
bool configure_phy_cfg_from_sib1(srsran::phy_cfg_nr_t& phy_cfg, std::string& filename, uint32_t nbits)
{
  std::vector<uint8_t> sib1_raw(nbits);
  if (!read_raw_config(filename, sib1_raw.data(), nbits)) {
    printf("Failed to read SIB1 from %s\n", filename.c_str());
    return false;
  }

  asn1::rrc_nr::sib1_s sib1;
  if (!parse_to_sib1(sib1_raw.data(), nbits, sib1)) {
    printf("Failed to parse SIB1\n");
    return false;
  }
  update_phy_cfg_from_sib1(phy_cfg, sib1);
  return true;
}

/* Load RRC setup cell configuration from file and apply to phy cfg */
bool configure_phy_cfg_from_rrc_setup(srsran::phy_cfg_nr_t& phy_cfg,
                                      std::string&          filename,
                                      uint32_t              nbits,
                                      srslog::basic_logger& logger)
{
  std::vector<uint8_t> subpdu_raw(nbits);
  if (!read_raw_config(filename, subpdu_raw.data(), nbits)) {
    printf("Failed to read RRC setup from %s\n", filename.c_str());
    return false;
  }
  asn1::rrc_nr::dl_ccch_msg_s dl_ccch_msg;
  if (!parse_to_dl_ccch_msg(subpdu_raw.data(), nbits, dl_ccch_msg)) {
    printf("Failed to parse DL-CCCH message\n");
    return false;
  }
  if (dl_ccch_msg.msg.c1().type().value != asn1::rrc_nr::dl_ccch_msg_type_c::c1_c_::types::rrc_setup) {
    printf("Expected RRC setup message\n");
    return false;
  }

  asn1::rrc_nr::cell_group_cfg_s cell_group;
  if (!extract_cell_group_cfg(dl_ccch_msg, cell_group)) {
    printf("Failed to extract cell group config\n");
    return false;
  }

  srsran::static_circular_map<uint32_t, srsran_pucch_nr_resource_t, 128> pucch_res_list;
  std::map<uint32_t, srsran_csi_rs_zp_resource_t>                        csi_rs_zp_res;
  std::map<uint32_t, srsran_csi_rs_nzp_resource_t>                       csi_rs_nzp_res;
  if (cell_group.sp_cell_cfg_present) {
    if (!update_phy_cfg_from_cell_cfg(
            phy_cfg, cell_group.sp_cell_cfg, pucch_res_list, csi_rs_zp_res, csi_rs_nzp_res, logger)) {
      printf("Failed to update phy cfg from cell cfg\n");
      return false;
    }
  }
  if (cell_group.phys_cell_group_cfg_present) {
    switch (cell_group.phys_cell_group_cfg.pdsch_harq_ack_codebook) {
      case asn1::rrc_nr::phys_cell_group_cfg_s::pdsch_harq_ack_codebook_opts::dynamic_value:
        phy_cfg.harq_ack.harq_ack_codebook = srsran_pdsch_harq_ack_codebook_dynamic;
        break;
      case asn1::rrc_nr::phys_cell_group_cfg_s::pdsch_harq_ack_codebook_opts::semi_static:
        phy_cfg.harq_ack.harq_ack_codebook = srsran_pdsch_harq_ack_codebook_semi_static;
        break;
      case asn1::rrc_nr::phys_cell_group_cfg_s::pdsch_harq_ack_codebook_opts::nulltype:
        phy_cfg.harq_ack.harq_ack_codebook = srsran_pdsch_harq_ack_codebook_none;
        break;
      default:
        asn1::log_warning("Invalid option for pdsch_harq_ack_codebook %s",
                          cell_group.phys_cell_group_cfg.pdsch_harq_ack_codebook.to_string());
        return false;
    }
  }
  return true;
}

/* Apply MIB configuration to phy cfg */
bool update_phy_cfg_from_mib(srsran::phy_cfg_nr_t& phy_cfg, srsran_mib_nr_t& mib, uint32_t ncellid)
{
  phy_cfg.pdsch.typeA_pos = mib.dmrs_typeA_pos;
  phy_cfg.pdsch.scs_cfg   = mib.scs_common;
  phy_cfg.carrier.pci     = ncellid;

  /* Get pointA and SSB absolute frequencies */
  double pointA_abs_freq_Hz = phy_cfg.carrier.dl_center_frequency_hz -
                              phy_cfg.carrier.nof_prb * SRSRAN_NRE * SRSRAN_SUBC_SPACING_NR(phy_cfg.carrier.scs) / 2;
  double ssb_abs_freq_Hz = phy_cfg.carrier.ssb_center_freq_hz;
  /* Calculate integer SSB to pointA frequency offset in Hz */
  uint32_t ssb_pointA_freq_offset_Hz =
      (ssb_abs_freq_Hz > pointA_abs_freq_Hz) ? (uint32_t)(ssb_abs_freq_Hz - pointA_abs_freq_Hz) : 0;
  /* Create coreset0 */
  if (srsran_coreset_zero(phy_cfg.carrier.pci,
                          ssb_pointA_freq_offset_Hz,
                          phy_cfg.ssb.scs,
                          phy_cfg.carrier.scs,
                          mib.coreset0_idx,
                          &phy_cfg.pdcch.coreset[0])) {
    return false;
  }
  phy_cfg.pdcch.coreset_present[0] = true;

  /* Create SearchSpace0 */
  srsran::make_phy_search_space0_cfg(&phy_cfg.pdcch.search_space[0]);
  phy_cfg.pdcch.search_space_present[0] = true;
  return true;
}

/* Apply SIB1 configuration to phy cfg */
void update_phy_cfg_from_sib1(srsran::phy_cfg_nr_t& phy_cfg, asn1::rrc_nr::sib1_s& sib1)
{
  /* Apply PDSCH Config Common */
  if (sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup()
          .pdsch_time_domain_alloc_list.size() > 0) {
    if (!srsran::fill_phy_pdsch_cfg_common(
            sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdsch_cfg_common.setup(), &phy_cfg.pdsch)) {
    }
  }

  /* Apply PUSCH Config Common */
  if (!srsran::fill_phy_pusch_cfg_common(
          sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pusch_cfg_common.setup(), &phy_cfg.pusch)) {
  }

  /* Apply PUCCH Config Common */
  srsran::fill_phy_pucch_cfg_common(sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.pucch_cfg_common.setup(),
                                    &phy_cfg.pucch.common);

  /* Apply RACH Config Common */
  if (!srsran::make_phy_rach_cfg(sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.rach_cfg_common.setup(),
                                 sib1.serving_cell_cfg_common.tdd_ul_dl_cfg_common_present ? SRSRAN_DUPLEX_MODE_TDD
                                                                                           : SRSRAN_DUPLEX_MODE_FDD,
                                 &phy_cfg.prach)) {
  }

  /* Apply PDCCH Config Common */
  srsran::fill_phy_pdcch_cfg_common(sib1.serving_cell_cfg_common.dl_cfg_common.init_dl_bwp.pdcch_cfg_common.setup(),
                                    &phy_cfg.pdcch);

  /* Apply Carrier Config */
  srsran::fill_phy_carrier_cfg(sib1.serving_cell_cfg_common, &phy_cfg.carrier);

  /* Apply SSB Config */
  srsran::fill_phy_ssb_cfg(sib1.serving_cell_cfg_common, &phy_cfg.ssb);
  /* Apply n-TimingAdvanceOffset */
  if (sib1.serving_cell_cfg_common.n_timing_advance_offset_present) {
    switch (sib1.serving_cell_cfg_common.n_timing_advance_offset.value) {
      case asn1::rrc_nr::serving_cell_cfg_common_sib_s::n_timing_advance_offset_opts::n0:
        phy_cfg.t_offset = 0;
        break;
      case asn1::rrc_nr::serving_cell_cfg_common_sib_s::n_timing_advance_offset_opts::n25600:
        phy_cfg.t_offset = 25600;
        break;
      case asn1::rrc_nr::serving_cell_cfg_common_sib_s::n_timing_advance_offset_opts::n39936:
        phy_cfg.t_offset = 39936;
        break;
      default:
        break;
    }
  } else {
    phy_cfg.t_offset = 25600;
  }
  if (sib1.serving_cell_cfg_common.tdd_ul_dl_cfg_common_present) {
    srsran::make_phy_tdd_cfg(sib1.serving_cell_cfg_common.tdd_ul_dl_cfg_common, &phy_cfg.duplex);
  }
}

/* ue_dl related configuration and update, ue_dl decode messages send from base station to UE*/
bool init_ue_dl(srsran_ue_dl_nr_t& ue_dl, cf_t* buffer, srsran::phy_cfg_nr_t& phy_cfg)
{
  srsran_ue_dl_nr_args_t ue_dl_args             = {};
  ue_dl_args.nof_max_prb                        = phy_cfg.carrier.nof_prb;
  ue_dl_args.nof_rx_antennas                    = 1;
  ue_dl_args.pdcch.measure_evm                  = false;
  ue_dl_args.pdcch.measure_time                 = false;
  ue_dl_args.pdcch.disable_simd                 = false;
  ue_dl_args.pdsch.sch.disable_simd             = false;
  ue_dl_args.pdsch.sch.decoder_use_flooded      = false;
  ue_dl_args.pdsch.sch.decoder_scaling_factor   = 0;
  ue_dl_args.pdsch.sch.max_nof_iter             = 10;
  std::array<cf_t*, SRSRAN_MAX_PORTS> rx_buffer = {};
  rx_buffer[0]                                  = buffer;
  if (srsran_ue_dl_nr_init(&ue_dl, rx_buffer.data(), &ue_dl_args) != 0) {
    return false;
  }
  if (!update_ue_dl(ue_dl, phy_cfg)) {
    return false;
  }
  return true;
}

bool update_ue_dl(srsran_ue_dl_nr_t& ue_dl, srsran::phy_cfg_nr_t& phy_cfg)
{
  if (srsran_ue_dl_nr_set_carrier(&ue_dl, &phy_cfg.carrier) != SRSRAN_SUCCESS) {
    return false;
  }
  srsran_dci_cfg_nr_t dci_cfg = phy_cfg.get_dci_cfg();
  if (srsran_ue_dl_nr_set_pdcch_config(&ue_dl, &phy_cfg.pdcch, &dci_cfg) != SRSRAN_SUCCESS) {
    return false;
  }
  return true;
}

void ue_dl_dci_search(srsran_ue_dl_nr_t&    ue_dl,
                      srsran::phy_cfg_nr_t& phy_cfg,
                      srsran_slot_cfg_t&    slot_cfg,
                      uint16_t              rnti,
                      srsran_rnti_type_t    rnti_type,
                      srsue::nr::state&     phy_state,
                      srslog::basic_logger& logger,
                      uint32_t              task_idx)
{
  char dci_str[256];
  ue_dl.num_dl_dci = 0;
  ue_dl.num_ul_dci = 0;
  /* Estimate PDCCH channel for every configured CORESET for each slot */
  for (uint32_t i = 0; i < SRSRAN_UE_DL_NR_MAX_NOF_CORESET; i++) {
    if (ue_dl.cfg.coreset_present[i]) {
      srsran_dmrs_pdcch_estimate(&ue_dl.dmrs_pdcch[i], &slot_cfg, ue_dl.sf_symbols[0]);
    }
  }
  /* Function used to detect the DCI for DL within the slot*/
  std::array<srsran_dci_dl_nr_t, SRSRAN_SEARCH_SPACE_MAX_NOF_CANDIDATES_NR> dci_dl = {};
  int                                                                       num_dci_dl =
      srsran_ue_dl_nr_find_dl_dci(&ue_dl, &slot_cfg, rnti, rnti_type, dci_dl.data(), (uint32_t)dci_dl.size());
  ue_dl.num_dl_dci = num_dci_dl;
  for (int i = 0; i < num_dci_dl; i++) {
    phy_state.set_dl_pending_grant(phy_cfg, slot_cfg, dci_dl[i]);
    if (logger.debug.enabled()) {
      srsran_dci_dl_nr_to_str(&ue_dl.dci, &dci_dl[i], dci_str, 256);
      logger.debug("DCI DL slot %u %u: %s", task_idx, slot_cfg.idx, dci_str);
    }
  }
  /* Function used to detect the DCI for UL within the slot*/
  std::array<srsran_dci_ul_nr_t, SRSRAN_SEARCH_SPACE_MAX_NOF_CANDIDATES_NR> dci_ul = {};
  int                                                                       num_dci_ul =
      srsran_ue_dl_nr_find_ul_dci(&ue_dl, &slot_cfg, rnti, rnti_type, dci_ul.data(), (uint32_t)dci_ul.size());
  ue_dl.num_ul_dci = num_dci_ul;
  for (int i = 0; i < num_dci_ul; i++) {
    phy_state.set_ul_pending_grant(phy_cfg, slot_cfg, dci_ul[i]);
    if (logger.debug.enabled()) {
      srsran_dci_ul_nr_to_str(&ue_dl.dci, &dci_ul[i], dci_str, 256);
      logger.debug("DCI UL slot %u %u: %s", task_idx, slot_cfg.idx, dci_str);
    }
  }
}

bool ue_dl_pdsch_decode(srsran_ue_dl_nr_t&      ue_dl,
                        srsran_sch_cfg_nr_t&    pdsch_cfg,
                        srsran_slot_cfg_t&      slot_cfg,
                        srsran_pdsch_res_nr_t&  pdsch_res,
                        srsran_softbuffer_rx_t& softbuffer_rx,
                        srslog::basic_logger&   logger,
                        uint32_t                task_idx)
{
  /* Initialize softbuffer */
  srsran_softbuffer_rx_reset(&softbuffer_rx);
  pdsch_cfg.grant.tb[0].softbuffer.rx = &softbuffer_rx;

  /* call srsran API to decode pdsch message */
  if (srsran_ue_dl_nr_decode_pdsch(&ue_dl, &slot_cfg, &pdsch_cfg, &pdsch_res) != 0) {
    logger.error("Error srsran_ue_dl_nr_decode_pdsch");
    return false;
  }

  if (logger.debug.enabled()) {
    char str[256];
    srsran_ue_dl_nr_pdsch_info(&ue_dl, &pdsch_cfg, &pdsch_res, str, 256);
    logger.debug("PDSCH %u %u: %s", task_idx, slot_cfg.idx, str);
  }
  return true;
}

/* ue_ul related configuration and update, ue_ul encode messages send from UE to base station */
bool init_ue_ul(srsran_ue_ul_nr_t& ue_ul, cf_t* buffer, srsran::phy_cfg_nr_t& phy_cfg)
{
  srsran_ue_ul_nr_args_t ue_ul_args = {};
  ue_ul_args.nof_max_prb            = phy_cfg.carrier.nof_prb;
  ue_ul_args.pusch.max_prb          = phy_cfg.carrier.nof_prb;
  /* initialize UE ul instance and initialize the related buffers */
  if (srsran_ue_ul_nr_init(&ue_ul, buffer, &ue_ul_args) != 0) {
    return false;
  }
  if (srsran_ue_ul_nr_set_carrier(&ue_ul, &phy_cfg.carrier) != SRSRAN_SUCCESS) {
    return false;
  }
  return true;
}

bool update_ue_ul(srsran_ue_ul_nr_t& ue_ul, srsran::phy_cfg_nr_t& phy_cfg)
{
  if (srsran_ue_ul_nr_set_carrier(&ue_ul, &phy_cfg.carrier) != SRSRAN_SUCCESS) {
    return false;
  }
  return true;
}

/* gnb_dl related configuration and update, gnb_dl encode messages send from base station to UE */
bool init_gnb_dl(srsran_gnb_dl_t& gnb_dl, cf_t* buffer, srsran::phy_cfg_nr_t& phy_cfg, double srate)
{
  srsran_gnb_dl_args_t dl_args = {};
  dl_args.pdsch.measure_time   = true;
  dl_args.pdsch.max_layers     = 1;
  dl_args.pdsch.max_prb        = phy_cfg.carrier.nof_prb;
  dl_args.nof_max_prb          = phy_cfg.carrier.nof_prb;
  dl_args.nof_tx_antennas      = 1;
  dl_args.srate_hz             = srate;
  dl_args.scs                  = phy_cfg.carrier.scs;

  std::array<cf_t*, SRSRAN_MAX_PORTS> tx_buffer = {};
  tx_buffer[0]                                  = buffer;
  if (srsran_gnb_dl_init(&gnb_dl, tx_buffer.data(), &dl_args) != 0) {
    return false;
  }
  if (srsran_gnb_dl_set_carrier(&gnb_dl, &phy_cfg.carrier) != SRSRAN_SUCCESS) {
    return false;
  }
  return true;
}

bool update_gnb_dl(srsran_gnb_dl_t& gnb_dl, srsran::phy_cfg_nr_t& phy_cfg)
{
  if (srsran_gnb_dl_set_carrier(&gnb_dl, &phy_cfg.carrier) != SRSRAN_SUCCESS) {
    return false;
  }
  return true;
}

/* use gnb_dl to encode the message targeted to UE */
bool gnb_dl_encode(std::shared_ptr<std::vector<uint8_t> > msg,
                   srsran_gnb_dl_t&                       gnb_dl,
                   srsran_dci_cfg_nr_t&                   dci_cfg,
                   srsran::phy_cfg_nr_t&                  phy_cfg,
                   srsran_sch_cfg_nr_t&                   pdsch_cfg,
                   srsran_slot_cfg_t&                     slot_cfg,
                   uint16_t                               rnti,
                   srsran_rnti_type_t                     rnti_type,
                   srslog::basic_logger&                  logger,
                   uint32_t                               mcs,
                   uint32_t                               nof_prb_to_allocate)
{
  /* set RE grid to zero */
  if (srsran_gnb_dl_base_zero(&gnb_dl) < SRSRAN_SUCCESS) {
    logger.error("Error initialize gnb_dl by running srsran_gnb_dl_base_zero");
    return false;
  }
  /* update pdcch with dci_cfg */
  dci_cfg = phy_cfg.get_dci_cfg();
  if (srsran_gnb_dl_set_pdcch_config(&gnb_dl, &phy_cfg.pdcch, &dci_cfg) < SRSRAN_SUCCESS) {
    logger.error("Error set pdcch config");
    return false;
  }
  /* Build the DCI message */
  srsran_dci_dl_nr_t dci_to_send = {};
  if (!construct_dci_dl_to_send(dci_to_send, phy_cfg, slot_cfg.idx, rnti, rnti_type, mcs, nof_prb_to_allocate)) {
    logger.error("Error construct dci to send");
    return false;
  }
  /* Pack dci into pdcch */
  if (srsran_gnb_dl_pdcch_put_dl(&gnb_dl, &slot_cfg, &dci_to_send) < SRSRAN_SUCCESS) {
    logger.error("Error put dci into pdcch");
    return false;
  }
  /* pack the message to be sent */
  uint8_t* data_to_send[SRSRAN_MAX_TB] = {};
  uint8_t  payload[SRSRAN_SLOT_MAX_NOF_BITS_NR];
  memset(payload, 0, SRSRAN_SLOT_MAX_NOF_BITS_NR);
  data_to_send[0] = payload;
  memcpy(payload, msg->data(), msg->size());

  /* get pdsch cfg from phy_cfg */
  if (!phy_cfg.get_pdsch_cfg(slot_cfg, dci_to_send, pdsch_cfg)) {
    logger.error("Error get pdsch cfg");
    return false;
  }
  srsran_softbuffer_tx_t softbuffer_tx = {};
  if (srsran_softbuffer_tx_init_guru(&softbuffer_tx, SRSRAN_SCH_NR_MAX_NOF_CB_LDPC, SRSRAN_LDPC_MAX_LEN_ENCODED_CB) <
      SRSRAN_SUCCESS) {
    logger.error("Error initializing softbuffer");
    return false;
  }
  pdsch_cfg.grant.tb[0].softbuffer.tx = &softbuffer_tx;
  if (srsran_gnb_dl_pdsch_put(&gnb_dl, &slot_cfg, &pdsch_cfg, data_to_send) < SRSRAN_SUCCESS) {
    logger.error("Error putting PDSCH message");
    return false;
  }

  /* generate the actual signal */
  srsran_gnb_dl_gen_signal(&gnb_dl);
  return true;
}

/* Find a search space that contains target dci format */
bool find_search_space(srsran_search_space_t** search_space,
                       srsran::phy_cfg_nr_t&   phy_cfg,
                       srsran_dci_format_nr_t  format)
{
  for (uint32_t i = 1; i < SRSRAN_UE_DL_NR_MAX_NOF_SEARCH_SPACE; i++) {
    if (!phy_cfg.pdcch.search_space_present[i]) {
      continue;
    }
    srsran_search_space_t* current_search_space = &phy_cfg.pdcch.search_space[i];
    for (uint32_t j = 0; j < current_search_space->nof_formats; j++) {
      if (current_search_space->formats[j] == format) {
        *search_space = current_search_space;
        return true;
      }
    }
  }
  if (!phy_cfg.pdcch.search_space_present[0]) {
    return false;
  }
  srsran_search_space_t* search_space0 = &phy_cfg.pdcch.search_space[0];
  for (uint32_t j = 0; j < search_space0->nof_formats; j++) {
    if (search_space0->formats[j] == format) {
      *search_space = search_space0;
      return true;
    }
  }
  return false;
}

/* Find an aggregation level to use */
bool find_aggregation_level(srsran_dci_ctx_t&      dci_ctx,
                            srsran_coreset_t*      coreset,
                            srsran_search_space_t* search_space,
                            uint32_t               slot_idx,
                            uint16_t               rnti)
{
  for (uint32_t agl = 0; agl < SRSRAN_SEARCH_SPACE_NOF_AGGREGATION_LEVELS_NR; agl++) {
    uint32_t L                                                        = 1U << agl;
    uint32_t dci_locations[SRSRAN_SEARCH_SPACE_MAX_NOF_CANDIDATES_NR] = {};
    int      n = srsran_pdcch_nr_locations_coreset(coreset, search_space, rnti, agl, slot_idx, dci_locations);
    if (n < SRSRAN_SUCCESS) {
      return false;
    }
    if (n == 0) {
      continue;
    }
    for (uint32_t ncce_idx = 0; ncce_idx < n; ncce_idx++) {
      dci_ctx.location.L    = agl;
      dci_ctx.location.ncce = dci_locations[ncce_idx];
      return true;
    }
  }
  return false;
}

/* Construct the dci to send */
bool construct_dci_dl_to_send(srsran_dci_dl_nr_t&   dci_to_send,
                              srsran::phy_cfg_nr_t& phy_cfg,
                              uint32_t              slot_idx,
                              uint16_t              rnti,
                              srsran_rnti_type_t    rnti_type,
                              uint32_t              mcs,
                              uint32_t              nof_prb_to_allocate)
{
  /* find a search space to use */
  srsran_search_space_t* search_space = nullptr;
  if (!find_search_space(&search_space, phy_cfg, srsran_dci_format_nr_1_0)) {
    return false;
  }
  /* get the coreset corresponding to the search space */
  srsran_coreset_t* coreset = &phy_cfg.pdcch.coreset[search_space->coreset_id];
  /* Initialize start resource block */
  uint32_t start_rb = 0;
  if (SRSRAN_SEARCH_SPACE_IS_COMMON(search_space->type)) {
    start_rb = coreset->offset_rb;
  }
  /* Get bwp size */
  uint32_t coreset_bw   = srsran_coreset_get_bw(coreset);
  uint32_t type1_bwp_sz = phy_cfg.carrier.nof_prb;
  if (SRSRAN_SEARCH_SPACE_IS_COMMON(search_space->type) && coreset_bw != 0) {
    type1_bwp_sz = coreset_bw;
  }

  dci_to_send.mcs     = mcs;
  nof_prb_to_allocate = std::min(nof_prb_to_allocate, coreset_bw);
  if (SRSRAN_SEARCH_SPACE_IS_COMMON(search_space->type) && coreset_bw != 0) {
    type1_bwp_sz = coreset_bw;
  }
  uint32_t freq_domain_assignment    = srsran_ra_nr_type1_riv(type1_bwp_sz, 0, nof_prb_to_allocate); /* RIV */
  dci_to_send.freq_domain_assignment = freq_domain_assignment;
  dci_to_send.time_domain_assignment = 0;
  dci_to_send.coreset0_bw            = coreset_bw;
  dci_to_send.ctx.coreset_id         = search_space->coreset_id;
  dci_to_send.ctx.coreset_start_rb   = coreset->offset_rb;
  dci_to_send.ctx.ss_type            = search_space->type;
  dci_to_send.ctx.rnti_type          = rnti_type;
  dci_to_send.ctx.rnti               = rnti;
  dci_to_send.ctx.format             = srsran_dci_format_nr_1_0;
  if (!find_aggregation_level(dci_to_send.ctx, coreset, search_space, slot_idx, rnti)) {
    return false;
  }
  return true;
}

bool construct_dci_ul_to_send(srsran_dci_ul_nr_t&   dci_to_send,
                              srsran::phy_cfg_nr_t& phy_cfg,
                              uint32_t              slot_idx,
                              uint16_t              rnti,
                              srsran_rnti_type_t    rnti_type,
                              uint32_t              mcs,
                              uint32_t              nof_prb_to_allocate)
{
  /* find a search space to use */
  srsran_search_space_t* search_space = nullptr;
  if (!find_search_space(&search_space, phy_cfg, srsran_dci_format_nr_0_0)) {
    return false;
  }
  /* get the coreset corresponding to the search space */
  srsran_coreset_t* coreset = &phy_cfg.pdcch.coreset[search_space->coreset_id];
  dci_to_send.mcs           = mcs;
  uint32_t coreset_bw       = srsran_coreset_get_bw(coreset);
  uint32_t freq_domain_assignment =
      srsran_ra_type2_to_riv(nof_prb_to_allocate, coreset->offset_rb, coreset_bw); /* RIV */
  dci_to_send.freq_domain_assignment = freq_domain_assignment;
  dci_to_send.time_domain_assignment = 0;
  dci_to_send.ndi                    = 1;
  dci_to_send.tpc                    = 1;
  dci_to_send.ctx.coreset_id         = coreset->id;
  dci_to_send.ctx.coreset_start_rb   = coreset->offset_rb;
  dci_to_send.ctx.ss_type            = search_space->type;
  dci_to_send.ctx.rnti_type          = rnti_type;
  dci_to_send.ctx.rnti               = rnti;
  dci_to_send.ctx.format             = srsran_dci_format_nr_0_0;
  if (!find_aggregation_level(dci_to_send.ctx, coreset, search_space, slot_idx, rnti)) {
    return false;
  }
  return true;
}

/* gnb_ul related configuration and update, gnb_ul decode messages send from UE to base station */
bool init_gnb_ul(srsran_gnb_ul_t& gnb_ul, cf_t* buffer, srsran::phy_cfg_nr_t& phy_cfg)
{
  srsran_gnb_ul_args_t ul_args   = {};
  ul_args.pusch.measure_time     = true;
  ul_args.pusch.measure_evm      = true;
  ul_args.pusch.max_layers       = 1;
  ul_args.pusch.sch.max_nof_iter = 10;
  ul_args.pusch.max_prb          = phy_cfg.carrier.nof_prb;
  ul_args.nof_max_prb            = phy_cfg.carrier.nof_prb;
  ul_args.pusch_min_snr_dB       = -10;
  if (srsran_gnb_ul_init(&gnb_ul, buffer, &ul_args) != 0) {
    return false;
  }
  if (srsran_gnb_ul_set_carrier(&gnb_ul, &phy_cfg.carrier) != SRSRAN_SUCCESS) {
    return false;
  }
  return true;
}

bool update_gnb_ul(srsran_gnb_ul_t& gnb_ul, srsran::phy_cfg_nr_t& phy_cfg)
{
  if (srsran_gnb_ul_set_carrier(&gnb_ul, &phy_cfg.carrier) != SRSRAN_SUCCESS) {
    return false;
  }
  return true;
}

/* Detect and decode PUSCH info bytes */
bool gnb_ul_pusch_decode(srsran_gnb_ul_t&        gnb_ul,
                         srsran_sch_cfg_nr_t&    pusch_cfg,
                         srsran_slot_cfg_t&      slot_cfg,
                         srsran_pusch_res_nr_t&  pusch_res,
                         srsran_softbuffer_rx_t& softbuffer_rx,
                         srslog::basic_logger&   logger,
                         uint32_t                task_idx)
{
  /* Run pusch channel estimation */
  if (srsran_dmrs_sch_estimate(
          &gnb_ul.dmrs, &slot_cfg, &pusch_cfg, &pusch_cfg.grant, gnb_ul.sf_symbols[0], &gnb_ul.chest_pusch)) {
    logger.error("Error running srsran_dmrs_sch_estimate");
    return false;
  }
  /* if SNR is too low, return false */
  if (gnb_ul.dmrs.csi.snr_dB < gnb_ul.pusch_min_snr_dB) {
    logger.debug("SNR is too low for PUSCH decoding: slot %u", slot_cfg.idx);
    if (logger.debug.enabled()) {
      char str[256];
      srsran_gnb_ul_pusch_info(&gnb_ul, &pusch_cfg, &pusch_res, str, 256);
      logger.info("PUSCH %u %u: %s", task_idx, slot_cfg.idx, str);
    }
    return false;
  }
  /* pusch and softbuffer initialization */
  srsran_softbuffer_rx_reset(&softbuffer_rx);
  pusch_cfg.grant.tb[0].softbuffer.rx = &softbuffer_rx;
  /* pusch decoding */
  if (srsran_pusch_nr_decode(
          &gnb_ul.pusch, &pusch_cfg, &pusch_cfg.grant, &gnb_ul.chest_pusch, gnb_ul.sf_symbols, &pusch_res)) {
    logger.error("Error running srsran_pusch_nr_decode\n");
    return false;
  }
  if (logger.debug.enabled()) {
    char str[256];
    srsran_gnb_ul_pusch_info(&gnb_ul, &pusch_cfg, &pusch_res, str, 256);
    logger.debug("PUSCH %u %u: %s", task_idx, slot_cfg.idx, str);
  }
  return true;
}

/* pdcch configuration update from cell config */
static bool apply_sp_cell_init_dl_pdcch(srsran::phy_cfg_nr_t&            phy_cfg,
                                        const asn1::rrc_nr::pdcch_cfg_s& pdcch_cfg,
                                        srslog::basic_logger&            logger)
{
  if (pdcch_cfg.search_spaces_to_add_mod_list.size() > 0) {
    for (uint32_t i = 0; i < pdcch_cfg.search_spaces_to_add_mod_list.size(); i++) {
      srsran_search_space_t search_space;
      if (srsran::make_phy_search_space_cfg(pdcch_cfg.search_spaces_to_add_mod_list[i], &search_space) == true) {
        phy_cfg.pdcch.search_space[search_space.id]         = search_space;
        phy_cfg.pdcch.search_space_present[search_space.id] = true;
      } else {
        logger.warning("Warning while building search_space structure id=%d", i);
        return false;
      }
    }
  } else {
    logger.warning("Option search_spaces_to_add_mod_list not present");
    return false;
  }
  if (pdcch_cfg.ctrl_res_set_to_add_mod_list.size() > 0) {
    for (uint32_t i = 0; i < pdcch_cfg.ctrl_res_set_to_add_mod_list.size(); i++) {
      srsran_coreset_t coreset;
      if (srsran::make_phy_coreset_cfg(pdcch_cfg.ctrl_res_set_to_add_mod_list[i], &coreset) == true) {
        phy_cfg.pdcch.coreset[coreset.id]         = coreset;
        phy_cfg.pdcch.coreset_present[coreset.id] = true;
      } else {
        logger.warning("Warning while building coreset structure");
        return false;
      }
    }
  } else {
    logger.warning("Option ctrl_res_set_to_add_mod_list not present");
  }
  return true;
}

static bool apply_csi_meas_cfg(srsran::phy_cfg_nr_t&                            phy_cfg,
                               asn1::rrc_nr::csi_meas_cfg_s&                    csi_meas_cfg,
                               std::map<uint32_t, srsran_csi_rs_nzp_resource_t> csi_rs_nzp_res,
                               srslog::basic_logger&                            logger)
{
  for (uint32_t i = 0; i < csi_meas_cfg.nzp_csi_rs_res_to_add_mod_list.size(); i++) {
    srsran_csi_rs_nzp_resource_t csi_rs_nzp_resource;
    if (srsran::make_phy_nzp_csi_rs_resource(csi_meas_cfg.nzp_csi_rs_res_to_add_mod_list[i], &csi_rs_nzp_resource) ==
        true) {
      csi_rs_nzp_res[csi_rs_nzp_resource.id] = csi_rs_nzp_resource;
    } else {
      logger.warning("Warning while building nzp_csi_rs resource");
      return false;
    }
  }

  for (uint32_t i = 0; i < csi_meas_cfg.nzp_csi_rs_res_set_to_add_mod_list.size(); i++) {
    uint8_t set_id = csi_meas_cfg.nzp_csi_rs_res_set_to_add_mod_list[i].nzp_csi_res_set_id;
    for (uint32_t j = 0; j < csi_meas_cfg.nzp_csi_rs_res_set_to_add_mod_list[i].nzp_csi_rs_res.size(); j++) {
      uint8_t res = csi_meas_cfg.nzp_csi_rs_res_set_to_add_mod_list[i].nzp_csi_rs_res[j];
      if (csi_rs_nzp_res.find(res) == csi_rs_nzp_res.end()) {
        logger.warning("Cannot find nzp_csi_rs_res in temporally stored csi_rs_nzp_res");
        return false;
      }
      phy_cfg.pdsch.nzp_csi_rs_sets[set_id].data[j] = csi_rs_nzp_res[res];
      phy_cfg.pdsch.nzp_csi_rs_sets[set_id].count += 1;
    }
    if (csi_meas_cfg.nzp_csi_rs_res_set_to_add_mod_list[i].trs_info_present) {
      phy_cfg.pdsch.nzp_csi_rs_sets[set_id].trs_info = true;
    }
  }
  return true;
}

/* pdsch configuration update from cell config */
static bool apply_sp_cell_init_dl_pdsch(srsran::phy_cfg_nr_t&                           phy_cfg,
                                        const asn1::rrc_nr::pdsch_cfg_s&                pdsch_cfg,
                                        std::map<uint32_t, srsran_csi_rs_zp_resource_t> csi_rs_zp_res,
                                        srslog::basic_logger&                           logger)
{
  if (pdsch_cfg.mcs_table_present) {
    switch (pdsch_cfg.mcs_table) {
      case asn1::rrc_nr::pdsch_cfg_s::mcs_table_opts::qam256:
        phy_cfg.pdsch.mcs_table = srsran_mcs_table_256qam;
        break;
      case asn1::rrc_nr::pdsch_cfg_s::mcs_table_opts::qam64_low_se:
        phy_cfg.pdsch.mcs_table = srsran_mcs_table_qam64LowSE;
        break;
      case asn1::rrc_nr::pdsch_cfg_s::mcs_table_opts::nulltype:
        logger.warning("Warning while selecting pdsch mcs_table");
        return false;
    }
  } else {
    // If the field is absent the UE applies the value 64QAM.
    phy_cfg.pdsch.mcs_table = srsran_mcs_table_64qam;
  }

  if (pdsch_cfg.dmrs_dl_for_pdsch_map_type_a_present) {
    if (pdsch_cfg.dmrs_dl_for_pdsch_map_type_a.type() ==
        asn1::setup_release_c<asn1::rrc_nr::dmrs_dl_cfg_s>::types_opts::setup) {
      // See TS 38.331, DMRS-DownlinkConfig. Also, see TS 38.214, 5.1.6.2 - DM-RS reception procedure.
      phy_cfg.pdsch.dmrs_typeA.additional_pos = srsran_dmrs_sch_add_pos_2;
      phy_cfg.pdsch.dmrs_typeA.present        = true;
    } else {
      logger.warning("Option dmrs_dl_for_pdsch_map_type_a not of type setup");
      return false;
    }
  } else {
    logger.warning("Option dmrs_dl_for_pdsch_map_type_a not present");
    return false;
  }

  srsran_resource_alloc_t resource_alloc;
  if (srsran::make_phy_pdsch_alloc_type(pdsch_cfg, &resource_alloc) == true) {
    phy_cfg.pdsch.alloc = resource_alloc;
  }
  if (pdsch_cfg.zp_csi_rs_res_to_add_mod_list.size() > 0) {
    for (uint32_t i = 0; i < pdsch_cfg.zp_csi_rs_res_to_add_mod_list.size(); i++) {
      srsran_csi_rs_zp_resource_t zp_csi_rs_resource;
      if (srsran::make_phy_zp_csi_rs_resource(pdsch_cfg.zp_csi_rs_res_to_add_mod_list[i], &zp_csi_rs_resource) ==
          true) {
        // temporally store csi_rs_zp_res
        csi_rs_zp_res[zp_csi_rs_resource.id] = zp_csi_rs_resource;
      } else {
        logger.warning("Warning while building zp_csi_rs resource");
        return false;
      }
    }
  }

  if (pdsch_cfg.p_zp_csi_rs_res_set_present) {
    // check if resources have been processed
    if (pdsch_cfg.zp_csi_rs_res_to_add_mod_list.size() == 0) {
      logger.warning("Can't build ZP-CSI config, option zp_csi_rs_res_to_add_mod_list not present");
      return false;
    }
    if (pdsch_cfg.p_zp_csi_rs_res_set.type() ==
        asn1::setup_release_c<asn1::rrc_nr::zp_csi_rs_res_set_s>::types_opts::setup) {
      for (uint32_t i = 0; i < pdsch_cfg.p_zp_csi_rs_res_set.setup().zp_csi_rs_res_id_list.size(); i++) {
        uint8_t res = pdsch_cfg.p_zp_csi_rs_res_set.setup().zp_csi_rs_res_id_list[i];
        // use temporally stored values to assign
        if (csi_rs_zp_res.find(res) == csi_rs_zp_res.end()) {
          logger.warning("Can not find p_zp_csi_rs_res in temporally stored csi_rs_zp_res");
          return false;
        }
        phy_cfg.pdsch.p_zp_csi_rs_set.data[i] = csi_rs_zp_res[res];
        phy_cfg.pdsch.p_zp_csi_rs_set.count += 1;
      }
    } else {
      logger.warning("Option p_zp_csi_rs_res_set not of type setup");
      return false;
    }
  }
  return true;
}

/* pucch configuration update from cell config */
static bool
apply_sp_cell_ded_ul_pucch(srsran::phy_cfg_nr_t&                                                    phy_cfg,
                           const asn1::rrc_nr::pucch_cfg_s&                                         pucch_cfg,
                           srsran::static_circular_map<uint32_t, srsran_pucch_nr_resource_t, 128UL> pucch_res_list,
                           srslog::basic_logger&                                                    logger)
{
  // determine format 2 max code rate
  uint32_t format_2_max_code_rate = 0;
  if (pucch_cfg.format2_present &&
      pucch_cfg.format2.type() == asn1::setup_release_c<asn1::rrc_nr::pucch_format_cfg_s>::types::setup) {
    if (pucch_cfg.format2.setup().max_code_rate_present) {
      if (srsran::make_phy_max_code_rate(pucch_cfg.format2.setup(), &format_2_max_code_rate) == false) {
        logger.warning("Warning while building format_2_max_code_rate");
      }
    }
  } else {
    logger.warning("Option format2 not present or not of type setup");
    return false;
  }

  // now look up resource and assign into internal struct
  if (pucch_cfg.res_to_add_mod_list.size() > 0) {
    for (uint32_t i = 0; i < pucch_cfg.res_to_add_mod_list.size(); i++) {
      uint32_t res_id = pucch_cfg.res_to_add_mod_list[i].pucch_res_id;
      pucch_res_list.insert(res_id, {});
      if (!srsran::make_phy_res_config(
              pucch_cfg.res_to_add_mod_list[i], format_2_max_code_rate, &pucch_res_list[res_id])) {
        logger.warning("Warning while building pucch_nr_resource structure");
        return false;
      }
    }
  } else {
    logger.warning("Option res_to_add_mod_list not present");
    return false;
  }

  // Check first all resource lists and
  phy_cfg.pucch.enabled = true;
  if (pucch_cfg.res_set_to_add_mod_list.size() > 0) {
    for (uint32_t i = 0; i < pucch_cfg.res_set_to_add_mod_list.size(); i++) {
      uint32_t set_id                          = pucch_cfg.res_set_to_add_mod_list[i].pucch_res_set_id;
      phy_cfg.pucch.sets[set_id].nof_resources = pucch_cfg.res_set_to_add_mod_list[i].res_list.size();
      for (uint32_t j = 0; j < pucch_cfg.res_set_to_add_mod_list[i].res_list.size(); j++) {
        uint32_t res_id = pucch_cfg.res_set_to_add_mod_list[i].res_list[j];
        if (pucch_res_list.contains(res_id)) {
          phy_cfg.pucch.sets[set_id].resources[j] = pucch_res_list[res_id];
        } else {
          logger.error(
              "Resources set not present for assign pucch sets (res_id %d, setid %d, j %d)", res_id, set_id, j);
        }
      }
    }
  }

  if (pucch_cfg.sched_request_res_to_add_mod_list.size() > 0) {
    for (uint32_t i = 0; i < pucch_cfg.sched_request_res_to_add_mod_list.size(); i++) {
      uint32_t                      sr_res_id = pucch_cfg.sched_request_res_to_add_mod_list[i].sched_request_res_id;
      srsran_pucch_nr_sr_resource_t srsran_pucch_nr_sr_resource;
      if (srsran::make_phy_sr_resource(pucch_cfg.sched_request_res_to_add_mod_list[i], &srsran_pucch_nr_sr_resource) ==
          true) { // TODO: fix that if indexing is solved
        phy_cfg.pucch.sr_resources[sr_res_id] = srsran_pucch_nr_sr_resource;

        // Set PUCCH resource
        if (pucch_cfg.sched_request_res_to_add_mod_list[i].res_present) {
          uint32_t pucch_res_id = pucch_cfg.sched_request_res_to_add_mod_list[i].res;
          if (pucch_res_list.contains(pucch_res_id)) {
            phy_cfg.pucch.sr_resources[sr_res_id].resource = pucch_res_list[pucch_res_id];
          } else {
            logger.warning("Warning SR (%d) PUCCH resource is invalid (%d)", sr_res_id, pucch_res_id);
            phy_cfg.pucch.sr_resources[sr_res_id].configured = false;
            return false;
          }
        } else {
          logger.warning("Warning SR resource is present but no PUCCH resource is assigned to it");
          phy_cfg.pucch.sr_resources[sr_res_id].configured = false;
          return false;
        }

      } else {
        logger.warning("Warning while building srsran_pucch_nr_sr_resource structure");
        return false;
      }
    }
  } else {
    logger.warning("Option sched_request_res_to_add_mod_list not present");
    return false;
  }

  if (pucch_cfg.dl_data_to_ul_ack.size() > 0) {
    for (uint32_t i = 0; i < pucch_cfg.dl_data_to_ul_ack.size(); i++) {
      phy_cfg.harq_ack.dl_data_to_ul_ack[i] = pucch_cfg.dl_data_to_ul_ack[i];
    }
    phy_cfg.harq_ack.nof_dl_data_to_ul_ack = pucch_cfg.dl_data_to_ul_ack.size();
  } else {
    logger.warning("Option dl_data_to_ul_ack not present");
    return false;
  }

  return true;
};

/* pusch configuration update from cell config */
static bool apply_sp_cell_ded_ul_pusch(srsran::phy_cfg_nr_t&            phy_cfg,
                                       const asn1::rrc_nr::pusch_cfg_s& pusch_cfg,
                                       srslog::basic_logger&            logger)
{
  if (pusch_cfg.mcs_table_present) {
    switch (pusch_cfg.mcs_table) {
      case asn1::rrc_nr::pusch_cfg_s::mcs_table_opts::qam256:
        phy_cfg.pusch.mcs_table = srsran_mcs_table_256qam;
        break;
      case asn1::rrc_nr::pusch_cfg_s::mcs_table_opts::qam64_low_se:
        phy_cfg.pusch.mcs_table = srsran_mcs_table_qam64LowSE;
        break;
      case asn1::rrc_nr::pusch_cfg_s::mcs_table_opts::nulltype:
        logger.warning("Warning while selecting pusch mcs_table");
        return false;
    }
  } else {
    // If the field is absent the UE applies the value 64QAM.
    phy_cfg.pusch.mcs_table = srsran_mcs_table_64qam;
  }

  srsran_resource_alloc_t resource_alloc;
  if (srsran::make_phy_pusch_alloc_type(pusch_cfg, &resource_alloc) == true) {
    phy_cfg.pusch.alloc = resource_alloc;
  }

  if (pusch_cfg.dmrs_ul_for_pusch_map_type_a_present) {
    if (pusch_cfg.dmrs_ul_for_pusch_map_type_a.type() ==
        asn1::setup_release_c<asn1::rrc_nr::dmrs_ul_cfg_s>::types_opts::setup) {
      // See TS 38.331, DMRS-UplinkConfig. Also, see TS 38.214, 6.2.2 - UE DM-RS transmission procedure.
      phy_cfg.pusch.dmrs_typeA.additional_pos = srsran_dmrs_sch_add_pos_2;
      phy_cfg.pusch.dmrs_typeA.present        = true;
    } else {
      logger.warning("Option dmrs_ul_for_pusch_map_type_a not of type setup");
      return false;
    }
  } else {
    logger.warning("Option dmrs_ul_for_pusch_map_type_a not present");
    return false;
  }
  if (pusch_cfg.uci_on_pusch_present) {
    if (pusch_cfg.uci_on_pusch.type() == asn1::setup_release_c<asn1::rrc_nr::uci_on_pusch_s>::types_opts::setup) {
      if (pusch_cfg.uci_on_pusch.setup().beta_offsets_present) {
        if (pusch_cfg.uci_on_pusch.setup().beta_offsets.type() ==
            asn1::rrc_nr::uci_on_pusch_s::beta_offsets_c_::types_opts::semi_static) {
          srsran_beta_offsets_t beta_offsets;
          if (srsran::make_phy_beta_offsets(pusch_cfg.uci_on_pusch.setup().beta_offsets.semi_static(), &beta_offsets) ==
              true) {
            phy_cfg.pusch.beta_offsets = beta_offsets;
          } else {
            logger.warning("Warning while building beta_offsets structure");
            return false;
          }
        } else {
          logger.warning("Option beta_offsets not of type semi_static");
          return false;
        }
        if (pusch_cfg.uci_on_pusch_present) {
          if (srsran::make_phy_pusch_scaling(pusch_cfg.uci_on_pusch.setup(), &phy_cfg.pusch.scaling) == false) {
            logger.warning("Warning while building scaling structure");
            return false;
          }
        }
      } else {
        logger.warning("Option beta_offsets not present");
        return false;
      }
    } else {
      logger.warning("Option uci_on_pusch of type setup");
      return false;
    }
  } else {
    logger.warning("Option uci_on_pusch not present");
    return false;
  }
  return true;
};

/* Apply cell configuration to phy cfg, Copy from file rrc_nr.cc */
bool update_phy_cfg_from_cell_cfg(srsran::phy_cfg_nr_t&                                                  phy_cfg,
                                  asn1::rrc_nr::sp_cell_cfg_s&                                           sp_cell_cfg,
                                  srsran::static_circular_map<uint32_t, srsran_pucch_nr_resource_t, 128> pucch_res_list,
                                  std::map<uint32_t, srsran_csi_rs_zp_resource_t>                        csi_rs_zp_res,
                                  std::map<uint32_t, srsran_csi_rs_nzp_resource_t>                       csi_rs_nzp_res,
                                  srslog::basic_logger&                                                  logger)
{
  // NSA specific handling to defer CSI, SR, SRS config until after RA (see TS 38.331, Section 5.3.5.3)
  srsran_csi_hl_cfg_t prev_csi = phy_cfg.csi;
  // Dedicated config
  if (sp_cell_cfg.sp_cell_cfg_ded_present) {
    // Dedicated Downlink
    if (sp_cell_cfg.sp_cell_cfg_ded.init_dl_bwp_present) {
      if (sp_cell_cfg.sp_cell_cfg_ded.init_dl_bwp.pdcch_cfg_present) {
        if (sp_cell_cfg.sp_cell_cfg_ded.init_dl_bwp.pdcch_cfg.type() ==
            asn1::setup_release_c<asn1::rrc_nr::pdcch_cfg_s>::types_opts::setup) {
          if (apply_sp_cell_init_dl_pdcch(phy_cfg, sp_cell_cfg.sp_cell_cfg_ded.init_dl_bwp.pdcch_cfg.setup(), logger) ==
              false) {
            return false;
          }
        } else {
          logger.warning("Option pdcch_cfg not of type setup");
          return false;
        }
      } else {
        logger.warning("Option pdcch_cfg not present");
        return false;
      }
      if (sp_cell_cfg.sp_cell_cfg_ded.init_dl_bwp.pdsch_cfg_present) {
        if (sp_cell_cfg.sp_cell_cfg_ded.init_dl_bwp.pdsch_cfg.type() ==
            asn1::setup_release_c<asn1::rrc_nr::pdsch_cfg_s>::types_opts::setup) {
          if (apply_sp_cell_init_dl_pdsch(
                  phy_cfg, sp_cell_cfg.sp_cell_cfg_ded.init_dl_bwp.pdsch_cfg.setup(), csi_rs_zp_res, logger) == false) {
            logger.error("Couldn't apply PDSCH config for initial DL BWP in SpCell Cfg dedicated");
            return false;
          };
        } else {
          logger.warning("Option pdsch_cfg_cfg not of type setup");
          return false;
        }
      } else {
        logger.warning("Option pdsch_cfg not present");
        return false;
      }
    } else {
      logger.warning("Option init_dl_bwp not present");
      return false;
    }
    if (sp_cell_cfg.sp_cell_cfg_ded.csi_meas_cfg_present) {
      if (!apply_csi_meas_cfg(phy_cfg, sp_cell_cfg.sp_cell_cfg_ded.csi_meas_cfg.setup(), csi_rs_nzp_res, logger)) {
        logger.error("Couldn't apply csi_meas_cfg to phy_cfg");
        return false;
      }
    }
    // Dedicated Uplink
    if (sp_cell_cfg.sp_cell_cfg_ded.ul_cfg_present) {
      if (sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.init_ul_bwp_present) {
        if (sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.init_ul_bwp.pucch_cfg_present) {
          if (sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.init_ul_bwp.pucch_cfg.type() ==
              asn1::setup_release_c<asn1::rrc_nr::pucch_cfg_s>::types_opts::setup) {
            if (apply_sp_cell_ded_ul_pucch(phy_cfg,
                                           sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.init_ul_bwp.pucch_cfg.setup(),
                                           pucch_res_list,
                                           logger) == false) {
              return false;
            }
          } else {
            logger.warning("Option pucch_cfg not of type setup");
            return false;
          }
        } else {
          logger.warning("Option pucch_cfg for initial UL BWP in spCellConfigDedicated not present");
          return false;
        }
        if (sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.init_ul_bwp.pusch_cfg_present) {
          if (sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.init_ul_bwp.pusch_cfg.type() ==
              asn1::setup_release_c<asn1::rrc_nr::pusch_cfg_s>::types_opts::setup) {
            if (apply_sp_cell_ded_ul_pusch(
                    phy_cfg, sp_cell_cfg.sp_cell_cfg_ded.ul_cfg.init_ul_bwp.pusch_cfg.setup(), logger) == false) {
              return false;
            }
          } else {
            logger.warning("Option pusch_cfg not of type setup");
            return false;
          }
        } else {
          logger.warning("Option pusch_cfg in spCellConfigDedicated not present");
          return false;
        }
      } else {
        logger.warning("Option init_ul_bwp in spCellConfigDedicated not present");
        return false;
      }
    } else {
      logger.warning("Option ul_cfg in spCellConfigDedicated not present");
      return false;
    }
  } else {
    logger.warning("Option sp_cell_cfg_ded not present");
    return false;
  }

  if (sp_cell_cfg.recfg_with_sync_present) {
    phy_cfg.csi = prev_csi;
  }
  return true;
}