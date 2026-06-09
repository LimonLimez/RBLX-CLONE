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

    PacketChat chat{};
    chat.playerId = 7;
    std::strncpy(chat.username, "ChatUser", sizeof(chat.username) - 1);
    std::strncpy(chat.message, "Hello from chat", sizeof(chat.message) - 1);
    std::vector<char> chatPacket = PacketProtocol::buildStructPacket(PacketType::CHAT, chat);
    assert(PacketProtocol::tryReadFrame(chatPacket, parsed) == PacketProtocol::ParseStatus::Complete);
    assert(parsed.type == PacketType::CHAT);
    assert(parsed.payloadSize == sizeof(PacketChat));

    PacketChat decodedChat{};
    std::memcpy(&decodedChat, parsed.payload, sizeof(decodedChat));
    assert(decodedChat.playerId == 7);
    assert(PacketProtocol::fixedString(decodedChat.username, sizeof(decodedChat.username)) == "ChatUser");
    assert(PacketProtocol::fixedString(decodedChat.message, sizeof(decodedChat.message)) == "Hello from chat");

    PacketHeader badChatHeader{PacketType::CHAT, static_cast<uint32_t>(sizeof(PacketChat) - 1)};
    std::vector<char> badChat(sizeof(PacketHeader) + sizeof(PacketChat));
    std::memcpy(badChat.data(), &badChatHeader, sizeof(badChatHeader));
    assert(PacketProtocol::tryReadFrame(badChat, parsed) == PacketProtocol::ParseStatus::Invalid);

    PacketHeader badHeader{PacketType::WORLD_STATE, PacketProtocol::MAX_PAYLOAD_SIZE + 1};
    std::vector<char> bad(sizeof(PacketHeader));
    std::memcpy(bad.data(), &badHeader, sizeof(badHeader));
    assert(PacketProtocol::tryReadFrame(bad, parsed) == PacketProtocol::ParseStatus::Invalid);

    std::cout << "network_protocol_tests passed\n";
    return 0;
}
