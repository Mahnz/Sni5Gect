# Updates

## v1.0 – Initial Release
The first release of Sni5Gect, supporting TDD bands with a 30 kHz subcarrier spacing.
Tested configurations:
- Frequency Bands: n78, n41 (TDD)
- Frequency: 3427.5 MHz, 2550.15 MHz
- Subcarrier Spacing: 30 kHz
- Bandwidth: 20–50 MHz
- MIMO Configuration: Single-input single-output (SISO)
- Distance: 0 meter to up to 20 meters (with amplifier)

## v2.0 – FDD Support
This release expands Sni5Gect to support FDD bands.
- Added FDD band support using SDRs with dual RX channels.
- Unified codebase for 15 kHz and 30 kHz subcarrier spacing.
- Expanded testing with the following setups:
    - Frequency Bands: n78, n41 (TDD), n3 (FDD)
    - Frequency: 3427.5 MHz, 2550.15 MHz, 1865.0 MHz
    - Subcarrier Spacing: 30 kHz, 15 KHz

**Note:**
Since FDD uses separate frequencies for downlink and uplink, an SDR with two RF front-ends (e.g., USRP X310) is required to capture IQ samples from both frequencies simultaneously (otherwise Sni5Gect is only capable of sniffing the downlink channel). A GPSDO is strongly recommended for frequency synchronization; without it, manual calibration may be necessary.