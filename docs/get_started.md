# Get Started
## Hardware Requirements
Sni5Gect utilizes a USRP Software Defined Radio (SDR) device to send and receive IQ samples during communication between a legitimate 5G base station and a UE. The following SDRs are supported:
- USRP B210 SDR
- USRP x310 SDR

Host Machine Recommendations:
- Minimum: 12-core CPU
- 16 GB RAM

Our setup consists of AMD 5950x processor with 32 GB memory.

## Installation
Sni5Gect is modified from [srsRAN](https://github.com/srsran/srsRAN_4G) for message decoding and injection and it depends on [WDissector](https://github.com/asset-group/5ghoul-5g-nr-attacks) for dissecting the received messages.

The simplest way to set up the framework is using docker. We have provided a Dockerfile to build the entire framework from scratch. You may use the following command to build the container from scratch.
```bash
docker compose build sni5gect
```

Then start the framework using:
```bash
docker compose up -d sni5gect
```

## Run Sni5Gect
The Sni5Gect executable is located in the `build/shadower` directory, and configuration files are available in the `configs` folder.
Please use the following command to run Sni5Gect:

```bash
docker exec -it sni5gect bash
./build/shadower/shadower configs/srsran-n78-20MHz-b210.yaml
```

### Use file recording
The easiest way to get started with Sni5Gect is to run it using a pre-recorded IQ sample file. We provide a sample for offline testing.

Download and Extract the example recording file from Zenodo:
```bash
wget https://zenodo.org/records/15601773/files/example-connection-samsung-srsran.zip
unzip example-connection-samsung-srsran.zip
```

Edit configs/config-srsran-n78-20MHz.conf and modify the `source` and `rf` section as follows:

```yaml
source:
  source_type: file
  source_params: /root/sni5gect/example_connection/example.fc32

rf:
  sample_rate: 23.04e6
  num_channels: 1
  uplink_cfo: 0 # Uplink Carrier Frequency Offset correction in Hz
  downlink_cfo: 0 # Downlink Carrier Frequency Offset (CFO) in Hz
```

Finally launch the sniffer using:

```bash
./build/shadower/shadower configs/srsran-n78-20MHz-b210.yaml
```

You should see output similar to the screenshot below:

<img src="https://raw.githubusercontent.com/asset-group/Sni5Gect-5GNR-sniffing-and-exploitation/main/images/example_recording.png"/>

### Use SDR

To test Sni5Gect with a live over-the-air signal using a Software Defined Radio (SDR), update the configuration file to use the SDR as the source.

Example `source` and `rf` Section for UHD-compatible SDR (e.g., USRP B200)
```yaml
source:
  source_type: uhd
  source_params: type=b200,master_clock_rate=23.04e6

rf:
  sample_rate: 23.04e6
  num_channels: 1
  uplink_cfo: -0.00054 # Uplink Carrier Frequency Offset correction in Hz
  downlink_cfo: 0 # Downlink Carrier Frequency Offset (CFO) in Hz
  padding:
    front_padding: 0 # Number of IQ samples to pad in front of each burst
    back_padding: 0 # Number of IQ samples to pad at the end of each burst
  channels:
    - rx_frequency: 3427.5e6
      tx_frequency: 3427.5e6
      rx_offset: 0
      tx_offset: 0
      rx_gain: 40
      tx_gain: 80
      enable: true
```

Then start the sniffer with:
```bash
./build/shadower/shadower configs/srsran-n78-20MHz-b210.yaml
```
Upon startup, Sni5Gect will do the following:

1. Search for the base station using the specified center and SSB frequencies.
2. Retrieve cell configuration from SIB1.
3. Detect RAR messages indicating a new UE connecting to the target base station.

<img src="https://raw.githubusercontent.com/asset-group/Sni5Gect-5GNR-sniffing-and-exploitation/main/images/sni5gect-waiting-for-UE.png"/>
