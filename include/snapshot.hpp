#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace shardgate {

enum class ProtocolType { Https, Tcp, Udp, Arp, Other };

inline const char* to_string(ProtocolType p) {
    switch (p) {
        case ProtocolType::Https:
            return "https";
        case ProtocolType::Tcp:
            return "tcp";
        case ProtocolType::Udp:
            return "udp";
        case ProtocolType::Arp:
            return "arp";
        default:
            return "other";
    }
}

inline ProtocolType to_protocol_type(std::string_view proto) {
    auto begin = proto.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) return ProtocolType::Other;
    auto end = proto.find_last_not_of(" \t\r\n");
    std::string upper(proto.substr(begin, end - begin + 1));
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    if (upper == "HTTPS" || upper == "TLS" || upper == "SSL")
        return ProtocolType::Https;
    if (upper == "TCP") return ProtocolType::Tcp;
    if (upper == "UDP") return ProtocolType::Udp;
    if (upper == "ARP") return ProtocolType::Arp;
    return ProtocolType::Other;
}

namespace json {

inline std::string get_string(std::string_view doc, std::string_view key) {
    std::string needle = "\"" + std::string(key) + "\"";
    auto pos = doc.find(needle);
    if (pos == std::string_view::npos) return {};
    pos = doc.find(':', pos + needle.size());
    if (pos == std::string_view::npos) return {};
    ++pos;
    while (pos < doc.size() &&
           std::isspace(static_cast<unsigned char>(doc[pos])))
        ++pos;
    if (pos >= doc.size() || doc[pos] != '"') return {};
    ++pos;
    std::string out;
    while (pos < doc.size() && doc[pos] != '"') {
        if (doc[pos] == '\\' && pos + 1 < doc.size()) ++pos;
        out.push_back(doc[pos]);
        ++pos;
    }
    return out;
}

inline long long get_int(std::string_view doc, std::string_view key,
                         long long fallback = 0) {
    std::string needle = "\"" + std::string(key) + "\"";
    auto pos = doc.find(needle);
    if (pos == std::string_view::npos) return fallback;
    pos = doc.find(':', pos + needle.size());
    if (pos == std::string_view::npos) return fallback;
    ++pos;
    while (pos < doc.size() &&
           std::isspace(static_cast<unsigned char>(doc[pos])))
        ++pos;
    bool neg = pos < doc.size() && doc[pos] == '-';
    if (neg) ++pos;
    if (pos >= doc.size() ||
        !std::isdigit(static_cast<unsigned char>(doc[pos])))
        return fallback;
    long long value = 0;
    while (pos < doc.size() &&
           std::isdigit(static_cast<unsigned char>(doc[pos])))
        value = value * 10 + (doc[pos++] - '0');
    return neg ? -value : value;
}

}  // namespace json

struct SnapshotMessage {
    std::string transaction_id;
    std::string client_id;
    long long version = 0;
    int source_port = 0;
    int dest_port = 0;
    std::string source_ip;
    std::string dest_ip;
    std::string source_mac;
    std::string dest_mac;
    std::string proto;

    static SnapshotMessage parse(std::string_view doc) {
        SnapshotMessage m;
        m.transaction_id = json::get_string(doc, "transaction_id");
        m.client_id = json::get_string(doc, "client_id");
        m.version = json::get_int(doc, "version");
        m.source_port = static_cast<int>(json::get_int(doc, "source_port"));
        m.dest_port = static_cast<int>(json::get_int(doc, "dest_port"));
        m.source_ip = json::get_string(doc, "source_ip");
        m.dest_ip = json::get_string(doc, "dest_ip");
        m.source_mac = json::get_string(doc, "source_mac");
        m.dest_mac = json::get_string(doc, "dest_mac");
        m.proto = json::get_string(doc, "proto");
        return m;
    }
};

}  // namespace shardgate
