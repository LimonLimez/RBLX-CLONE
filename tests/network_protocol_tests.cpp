#include "Network.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

int main() {
    PacketWelcome welcome{42};
    std::vector<char> packet = PacketProtocol::buildStructPacket(PacketType::WELCOME, welcome);

    std::vector<char> buffer(packet.begin(), packet.begin() + 3);
    PacketProtocol::ParsedPacket parsed;
    assert(PacketProtocol::tryReadFrame(buffer, parsed) == PacketProtocol::ParseStatus::NeedMoreData);

    buffer.insert(buffer.end(), packet.begin() + 3, packet.end());
    assert(PacketProtocol::tryReadFrame(buffer, parsed) == PacketProtocol::ParseStatus::Complete);
    assert(parsed.type == PacketType::WELCOME);
    assert(parsed.payloadSize == sizeof(PacketWelcome));

    PacketWelcome decoded{};
    std::memcpy(&decoded, parsed.payload, sizeof(decoded));
    assert(decoded.playerId == 42);

    PacketProtocol::consumeFrame(buffer, parsed.frameSize);
    assert(buffer.empty());

    std::vector<char> twoPackets = packet;
    twoPackets.insert(twoPackets.end(), packet.begin(), packet.end());
    assert(PacketProtocol::tryReadFrame(twoPackets, parsed) == PacketProtocol::ParseStatus::Complete);
    PacketProtocol::consumeFrame(twoPackets, parsed.frameSize);
    assert(PacketProtocol::tryReadFrame(twoPackets, parsed) == PacketProtocol::ParseStatus::Complete);

    PacketHeader badHeader{PacketType::WORLD_STATE, PacketProtocol::MAX_PAYLOAD_SIZE + 1};
    std::vector<char> bad(sizeof(PacketHeader));
    std::memcpy(bad.data(), &badHeader, sizeof(badHeader));
    assert(PacketProtocol::tryReadFrame(bad, parsed) == PacketProtocol::ParseStatus::Invalid);

    std::cout << "network_protocol_tests passed\n";
    return 0;
}
