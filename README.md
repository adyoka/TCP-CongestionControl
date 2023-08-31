# TCP Implementation Project

## Overview

This project focuses on implementing a simplified TCP protocol from scratch. The objective is to build a reliable data transfer mechanism and incorporate congestion control features, which are slow-start feature, congestion avoidance, and fast retransmit.

## Implementation

### Reliable Data Transfer
In the first part of the project, a basic Reliable Data Transfer protocol was implemented on top of UDP. The primary components included:

- Sender Implementation (rdt sender): This component was responsible for sending packets to the network based on a fixed window size. It managed cumulative acknowledgments and retransmissions upon packet loss.
- Receiver Implementation (rdt receiver): The receiver counterpart received packets, sent cumulative acknowledgments, and managed buffering for out-of-order packets.
- Network Emulation (MahiMahi): The network emulator MahiMahi was utilized to test sender and receiver functionality in an emulated network environment.
### TCP Congestion Control
In the second part, we delved into implementing a congestion control mechanism for the sender and receiver. The focus was on creating features similar to TCP Tahoe, including:

- Slow Start: This algorithm dynamically determines the congestion window size (CWND) for the sender. It starts with a small CWND and gradually increases it, keeping track of packet loss occurrences.
- Congestion Avoidance: Once CWND exceeds a certain threshold, the sender enters congestion avoidance mode. It increases CWND more cautiously, adapting to the network's capacity to avoid overloading it.
- Fast Retransmit: When the sender detects duplicate acknowledgments (indicating possible packet loss), it triggers a fast retransmit to retransmit lost packets quickly.

## How to Use This Project

To use this project, follow these steps:

1. Clone or download the project repository to your local machine.
2. Navigate to the respective directories for `rdt sender` and `rdt receiver`.
3. Compile and run the sender and receiver components. Ensure that the receiver is started before the sender.
4. You can utilize MahiMahi or similar tools to emulate different network conditions and test the robustness of the implementation.
