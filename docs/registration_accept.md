# Authentication Bypass: 5G AKA Bypass
This exploit utilizes $I_8$ 5G AKA Bypass from paper [Logic Gone Astray: A Security Analysis Framework for the Control Plane Protocols of 5G Basebands](https://www.usenix.org/conference/usenixsecurity24/presentation/tu). Only the Pixel 7 phone with the Exynos modem is affected.
After receiving `Registration Request` from the UE, Sni5Gect injects the plaintext `Registration Accept` message with security header 4. The UE will ignore the wrong MAC, accept the `Registration Accept` message, and reply with `Registration Complete` and `PDU Session Establishment Requests`. Since the core network receives such unexpected messages, it instructs the gNB to release the connection by sending the `RRC Release` message to terminate the connection immediately.
```conf
exploit: modules/lib_plaintext_registration_accept.so
```
Example output:

<img src="https://raw.githubusercontent.com/asset-group/Sni5Gect-5GNR-sniffing-and-exploitation/main/images/registration_accept.png"/>
