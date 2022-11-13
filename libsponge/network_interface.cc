#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address)
    , _ip_address(ip_address)
    , _arp_tbl()
    , _waiting_queue()
    , _waiting_arp_response() {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    // next_hop_ip is in ARP table
    auto arp_item = this->_arp_tbl.find(next_hop_ip);
    if (arp_item != this->_arp_tbl.end()) {
        EthernetFrame eth_frame;

        eth_frame.header() = {arp_item->second._eth_addr, this->_ethernet_address, EthernetHeader::TYPE_IPv4};

        eth_frame.payload() = dgram.serialize();

        _frames_out.push(eth_frame);
    } else {
        // do not send a second request if already sent in last 5 seconds
        if (this->_waiting_arp_response.find(next_hop_ip) == this->_waiting_arp_response.end()) {
            // send an ARP request
            ARPMessage arp_req;

            arp_req.opcode = ARPMessage::OPCODE_REQUEST;

            arp_req.sender_ethernet_address = this->_ethernet_address;
            arp_req.sender_ip_address = this->_ip_address.ipv4_numeric();

            arp_req.target_ethernet_address = {};  // want to get
            arp_req.target_ip_address = next_hop_ip;

            // broadcast an ARP request
            EthernetFrame eth_frame;
            eth_frame.header() = {ETHERNET_BROADCAST, this->_ethernet_address, EthernetHeader::TYPE_ARP};
            eth_frame.payload() = arp_req.serialize();
            _frames_out.push(eth_frame);

            // add the datagram to waiting queue
            this->_waiting_queue.push_back(NextHopDatagram{dgram, next_hop});

            // add the IP to the map which contains those have sent an ARP request in last 5 seconds
            this->_waiting_arp_response.insert(
                pair<uint32_t, size_t>(next_hop_ip, NetworkInterface::_ttl_wait_for_response));
        }
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // filter those frames that it's not destined to our address
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) {
        return nullopt;
    }

    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;

        // parse fails
        if (dgram.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;
        } else {
            return dgram;
        }
    } else if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_msg;

        // parse fails
        if (arp_msg.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;
        } else {
            const bool &is_arp_request = arp_msg.opcode == ARPMessage::OPCODE_REQUEST &&
                                         arp_msg.target_ip_address == this->_ip_address.ipv4_numeric();

            const bool &is_arp_reply = arp_msg.opcode == ARPMessage::OPCODE_REPLY &&
                                       arp_msg.target_ethernet_address == this->_ethernet_address;

            // if the ARP message is a request from others
            // and the ip address matches ours, reply the sender
            if (is_arp_request) {
                ARPMessage arp_reply;

                arp_reply.opcode = ARPMessage::OPCODE_REPLY;

                arp_reply.sender_ip_address = this->_ip_address.ipv4_numeric();
                arp_reply.sender_ethernet_address = this->_ethernet_address;

                arp_reply.target_ip_address = arp_msg.sender_ip_address;
                arp_reply.target_ethernet_address = arp_msg.sender_ethernet_address;

                // reply the sender
                EthernetFrame eth_frame;
                eth_frame.header() = {
                    arp_msg.sender_ethernet_address, this->_ethernet_address, EthernetHeader::TYPE_ARP};
                eth_frame.payload() = arp_reply.serialize();
                _frames_out.push(eth_frame);
            }

            // if the ARP message is a reply from others
            // and the ethernet address matches ours,
            if (is_arp_request || is_arp_reply) {
                // insert into `_arp_tbl`
                this->_arp_tbl.insert(pair<uint32_t, ARPItem>(
                    arp_msg.sender_ip_address, {arp_msg.sender_ethernet_address, NetworkInterface::_ttl_time_out}));

                // we have found the missing ARP item through broadcasting, now send it
                auto itr = this->_waiting_queue.begin();
                while (itr != this->_waiting_queue.end()) {
                    // find the datagram to be sent
                    if (itr->_next_hop_addr.ipv4_numeric() == arp_msg.sender_ip_address) {
                        this->send_datagram(itr->_ip_datagram, itr->_next_hop_addr);
                        itr = _waiting_queue.erase(itr);
                    } else {
                        itr++;
                    }
                }

                // remove the IP from the `map`: waiting for ARP response
                this->_waiting_arp_response.erase(arp_msg.sender_ip_address);
            }
            return nullopt;
        }
    } else {
        return nullopt;
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    // remove expired items from `arp_tbl`
    for (auto itr = this->_arp_tbl.begin(); itr != this->_arp_tbl.end();) {
        if (itr->second._ttl <= ms_since_last_tick) {
            itr = this->_arp_tbl.erase(itr);
        } else {
            itr->second._ttl -= ms_since_last_tick;
            itr++;
        }
    }

    // remove expired items from `waiting_arp_response_ip`
    for (auto itr = this->_waiting_arp_response.begin(); itr != this->_waiting_arp_response.end();) {
        if (itr->second <= ms_since_last_tick) {
            // resend an ARP request
            ARPMessage arp_req;

            arp_req.opcode = ARPMessage::OPCODE_REQUEST;

            arp_req.sender_ethernet_address = this->_ethernet_address;
            arp_req.sender_ip_address = this->_ip_address.ipv4_numeric();

            arp_req.target_ethernet_address = {};  // want to get
            arp_req.target_ip_address = itr->first;

            // broadcast an ARP request
            EthernetFrame eth_frame;
            eth_frame.header() = {ETHERNET_BROADCAST, this->_ethernet_address, EthernetHeader::TYPE_ARP};
            eth_frame.payload() = arp_req.serialize();
            _frames_out.push(eth_frame);

            itr->second = NetworkInterface::_ttl_wait_for_response;
        } else {
            itr->second -= ms_since_last_tick;
            itr++;
        }
    }
}
