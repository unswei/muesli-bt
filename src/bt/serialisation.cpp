#include "bt/serialisation.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace bt {
namespace {

constexpr std::array<char, 4> k_magic{'M', 'B', 'T', '1'};
constexpr std::uint32_t k_format_version = 1;
constexpr std::uint8_t k_endianness_little = 1;
constexpr std::uint32_t k_max_serialised_items = 1'000'000;

void write_exact(std::ofstream& out, const void* data, std::size_t size, const std::string& where) {
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!out) {
        throw std::runtime_error(where + ": write failure");
    }
}

void read_exact(std::ifstream& in, void* data, std::size_t size, const std::string& where) {
    in.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    if (!in) {
        throw std::runtime_error(where + ": unexpected end of file");
    }
}

void write_u8(std::ofstream& out, std::uint8_t v, const std::string& where) {
    write_exact(out, &v, sizeof(v), where);
}

void write_u32(std::ofstream& out, std::uint32_t v, const std::string& where) {
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(v & 0xFFu),
        static_cast<std::uint8_t>((v >> 8) & 0xFFu),
        static_cast<std::uint8_t>((v >> 16) & 0xFFu),
        static_cast<std::uint8_t>((v >> 24) & 0xFFu),
    };
    write_exact(out, bytes.data(), bytes.size(), where);
}

void write_u64(std::ofstream& out, std::uint64_t v, const std::string& where) {
    const std::array<std::uint8_t, 8> bytes{
        static_cast<std::uint8_t>(v & 0xFFu),
        static_cast<std::uint8_t>((v >> 8) & 0xFFu),
        static_cast<std::uint8_t>((v >> 16) & 0xFFu),
        static_cast<std::uint8_t>((v >> 24) & 0xFFu),
        static_cast<std::uint8_t>((v >> 32) & 0xFFu),
        static_cast<std::uint8_t>((v >> 40) & 0xFFu),
        static_cast<std::uint8_t>((v >> 48) & 0xFFu),
        static_cast<std::uint8_t>((v >> 56) & 0xFFu),
    };
    write_exact(out, bytes.data(), bytes.size(), where);
}

void write_i64(std::ofstream& out, std::int64_t v, const std::string& where) {
    write_u64(out, static_cast<std::uint64_t>(v), where);
}

void write_f64(std::ofstream& out, double v, const std::string& where) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v));
    std::memcpy(&bits, &v, sizeof(bits));
    write_u64(out, bits, where);
}

std::uint8_t read_u8(std::ifstream& in, const std::string& where) {
    std::uint8_t v = 0;
    read_exact(in, &v, sizeof(v), where);
    return v;
}

std::uint32_t read_u32(std::ifstream& in, const std::string& where) {
    std::array<std::uint8_t, 4> bytes{};
    read_exact(in, bytes.data(), bytes.size(), where);
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

std::uint64_t read_u64(std::ifstream& in, const std::string& where) {
    std::array<std::uint8_t, 8> bytes{};
    read_exact(in, bytes.data(), bytes.size(), where);
    return static_cast<std::uint64_t>(bytes[0]) |
           (static_cast<std::uint64_t>(bytes[1]) << 8u) |
           (static_cast<std::uint64_t>(bytes[2]) << 16u) |
           (static_cast<std::uint64_t>(bytes[3]) << 24u) |
           (static_cast<std::uint64_t>(bytes[4]) << 32u) |
           (static_cast<std::uint64_t>(bytes[5]) << 40u) |
           (static_cast<std::uint64_t>(bytes[6]) << 48u) |
           (static_cast<std::uint64_t>(bytes[7]) << 56u);
}

std::int64_t read_i64(std::ifstream& in, const std::string& where) {
    return static_cast<std::int64_t>(read_u64(in, where));
}

double read_f64(std::ifstream& in, const std::string& where) {
    const std::uint64_t bits = read_u64(in, where);
    double out = 0.0;
    static_assert(sizeof(out) == sizeof(bits));
    std::memcpy(&out, &bits, sizeof(bits));
    return out;
}

void write_string(std::ofstream& out, const std::string& text, const std::string& where) {
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::runtime_error(where + ": string too long");
    }
    write_u32(out, static_cast<std::uint32_t>(text.size()), where);
    if (!text.empty()) {
        write_exact(out, text.data(), text.size(), where);
    }
}

std::string read_string(std::ifstream& in, const std::string& where) {
    const std::uint32_t len = read_u32(in, where);
    if (len > k_max_serialised_items * 16) {
        throw std::runtime_error(where + ": string length is too large");
    }
    std::string out;
    out.resize(len);
    if (len > 0) {
        read_exact(in, out.data(), len, where);
    }
    return out;
}

bool is_valid_node_kind(std::uint8_t raw) {
    return raw <= static_cast<std::uint8_t>(node_kind::reactive_sel);
}

bool is_valid_arg_kind(std::uint8_t raw) {
    return raw <= static_cast<std::uint8_t>(arg_kind::string);
}

const char* node_kind_name(node_kind kind) {
    switch (kind) {
        case node_kind::seq:
            return "seq";
        case node_kind::sel:
            return "sel";
        case node_kind::invert:
            return "invert";
        case node_kind::repeat:
            return "repeat";
        case node_kind::retry:
            return "retry";
        case node_kind::cond:
            return "cond";
        case node_kind::act:
            return "act";
        case node_kind::succeed:
            return "succeed";
        case node_kind::fail:
            return "fail";
        case node_kind::running:
            return "running";
        case node_kind::plan_action:
            return "plan-action";
        case node_kind::vla_request:
            return "vla-request";
        case node_kind::vla_wait:
            return "vla-wait";
        case node_kind::vla_cancel:
            return "vla-cancel";
        case node_kind::mem_seq:
            return "mem-seq";
        case node_kind::mem_sel:
            return "mem-sel";
        case node_kind::async_seq:
            return "async-seq";
        case node_kind::reactive_seq:
            return "reactive-seq";
        case node_kind::reactive_sel:
            return "reactive-sel";
    }
    return "unknown";
}

std::string dot_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string node_label(const node& n) {
    std::ostringstream label;
    label << "id=" << n.id << "\n";
    if (!n.leaf_name.empty()) {
        label << n.leaf_name << "\n";
    }
    label << "[" << node_kind_name(n.kind);
    if (n.kind == node_kind::repeat || n.kind == node_kind::retry) {
        label << " " << n.int_param;
    }
    label << "]";
    return label.str();
}

void validate_definition(const definition& def) {
    if (def.nodes.empty()) {
        throw std::runtime_error("bt.load: definition has no nodes");
    }
    if (def.root >= def.nodes.size()) {
        throw std::runtime_error("bt.load: root node id out of range");
    }

    for (std::size_t i = 0; i < def.nodes.size(); ++i) {
        const node& n = def.nodes[i];
        if (n.id != i) {
            throw std::runtime_error("bt.load: node id mismatch");
        }
        for (node_id child : n.children) {
            if (child >= def.nodes.size()) {
                throw std::runtime_error("bt.load: child node id out of range");
            }
        }

        switch (n.kind) {
            case node_kind::seq:
            case node_kind::sel:
            case node_kind::mem_seq:
            case node_kind::mem_sel:
            case node_kind::async_seq:
            case node_kind::reactive_seq:
            case node_kind::reactive_sel:
                if (n.children.empty()) {
                    throw std::runtime_error("bt.load: composite nodes require at least one child");
                }
                break;
            case node_kind::invert:
            case node_kind::repeat:
            case node_kind::retry:
                if (n.children.size() != 1) {
                    throw std::runtime_error("bt.load: decorator nodes require exactly one child");
                }
                break;
            case node_kind::cond:
            case node_kind::act:
                if (n.leaf_name.empty()) {
                    throw std::runtime_error("bt.load: cond/act nodes require a leaf name");
                }
                break;
            case node_kind::succeed:
            case node_kind::fail:
            case node_kind::running:
                if (!n.children.empty()) {
                    throw std::runtime_error("bt.load: utility nodes cannot have children");
                }
                break;
            case node_kind::plan_action:
            case node_kind::vla_request:
            case node_kind::vla_wait:
            case node_kind::vla_cancel:
                if (!n.children.empty()) {
                    throw std::runtime_error("bt.load: planner/vla nodes cannot have children");
                }
                break;
        }
    }
}

}  // namespace

void save_definition_binary(const definition& def, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("bt.save: failed to open file: " + path);
    }

    if (def.nodes.size() > k_max_serialised_items) {
        throw std::runtime_error("bt.save: too many nodes to serialise");
    }

    write_exact(out, k_magic.data(), k_magic.size(), "bt.save");
    write_u32(out, k_format_version, "bt.save");
    write_u8(out, k_endianness_little, "bt.save");
    write_u8(out, 0, "bt.save");
    write_u8(out, 0, "bt.save");
    write_u8(out, 0, "bt.save");
    write_u32(out, static_cast<std::uint32_t>(def.nodes.size()), "bt.save");
    write_u32(out, static_cast<std::uint32_t>(def.root), "bt.save");

    for (const node& n : def.nodes) {
        write_u8(out, static_cast<std::uint8_t>(n.kind), "bt.save");
        write_u8(out, 0, "bt.save");
        write_u8(out, 0, "bt.save");
        write_u8(out, 0, "bt.save");
        write_i64(out, n.int_param, "bt.save");

        if (n.children.size() > k_max_serialised_items) {
            throw std::runtime_error("bt.save: node has too many children");
        }
        write_u32(out, static_cast<std::uint32_t>(n.children.size()), "bt.save");
        for (node_id child : n.children) {
            write_u32(out, static_cast<std::uint32_t>(child), "bt.save");
        }

        write_string(out, n.leaf_name, "bt.save");

        if (n.args.size() > k_max_serialised_items) {
            throw std::runtime_error("bt.save: node has too many args");
        }
        write_u32(out, static_cast<std::uint32_t>(n.args.size()), "bt.save");
        for (const arg_value& arg : n.args) {
            write_u8(out, static_cast<std::uint8_t>(arg.kind), "bt.save");
            switch (arg.kind) {
                case arg_kind::nil:
                    break;
                case arg_kind::boolean:
                    write_u8(out, arg.bool_v ? 1 : 0, "bt.save");
                    break;
                case arg_kind::integer:
                    write_i64(out, arg.int_v, "bt.save");
                    break;
                case arg_kind::floating:
                    write_f64(out, arg.float_v, "bt.save");
                    break;
                case arg_kind::symbol:
                case arg_kind::string:
                    write_string(out, arg.text, "bt.save");
                    break;
            }
        }
    }
}

definition load_definition_binary(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("bt.load: failed to open file: " + path);
    }

    std::array<char, 4> magic{};
    read_exact(in, magic.data(), magic.size(), "bt.load");
    if (magic != k_magic) {
        throw std::runtime_error("bt.load: invalid header (expected MBT1)");
    }

    const std::uint32_t version = read_u32(in, "bt.load");
    if (version != k_format_version) {
        throw std::runtime_error("bt.load: unsupported format version " + std::to_string(version));
    }

    const std::uint8_t endianness = read_u8(in, "bt.load");
    if (endianness != k_endianness_little) {
        throw std::runtime_error("bt.load: unsupported endianness marker");
    }
    (void)read_u8(in, "bt.load");
    (void)read_u8(in, "bt.load");
    (void)read_u8(in, "bt.load");

    const std::uint32_t node_count = read_u32(in, "bt.load");
    if (node_count == 0) {
        throw std::runtime_error("bt.load: file has no nodes");
    }
    if (node_count > k_max_serialised_items) {
        throw std::runtime_error("bt.load: node count is too large");
    }

    definition def;
    def.root = read_u32(in, "bt.load");
    def.nodes.resize(node_count);

    for (std::uint32_t i = 0; i < node_count; ++i) {
        node n;
        n.id = i;

        const std::uint8_t raw_kind = read_u8(in, "bt.load");
        if (!is_valid_node_kind(raw_kind)) {
            throw std::runtime_error("bt.load: invalid node kind");
        }
        n.kind = static_cast<node_kind>(raw_kind);

        (void)read_u8(in, "bt.load");
        (void)read_u8(in, "bt.load");
        (void)read_u8(in, "bt.load");

        n.int_param = read_i64(in, "bt.load");

        const std::uint32_t child_count = read_u32(in, "bt.load");
        if (child_count > k_max_serialised_items) {
            throw std::runtime_error("bt.load: child count is too large");
        }
        n.children.reserve(child_count);
        for (std::uint32_t child_idx = 0; child_idx < child_count; ++child_idx) {
            n.children.push_back(read_u32(in, "bt.load"));
        }

        n.leaf_name = read_string(in, "bt.load");

        const std::uint32_t arg_count = read_u32(in, "bt.load");
        if (arg_count > k_max_serialised_items) {
            throw std::runtime_error("bt.load: arg count is too large");
        }
        n.args.reserve(arg_count);
        for (std::uint32_t arg_idx = 0; arg_idx < arg_count; ++arg_idx) {
            arg_value arg;
            const std::uint8_t raw_arg_kind = read_u8(in, "bt.load");
            if (!is_valid_arg_kind(raw_arg_kind)) {
                throw std::runtime_error("bt.load: invalid arg kind");
            }
            arg.kind = static_cast<arg_kind>(raw_arg_kind);
            switch (arg.kind) {
                case arg_kind::nil:
                    break;
                case arg_kind::boolean:
                    arg.bool_v = read_u8(in, "bt.load") != 0;
                    break;
                case arg_kind::integer:
                    arg.int_v = read_i64(in, "bt.load");
                    break;
                case arg_kind::floating:
                    arg.float_v = read_f64(in, "bt.load");
                    break;
                case arg_kind::symbol:
                case arg_kind::string:
                    arg.text = read_string(in, "bt.load");
                    break;
            }
            n.args.push_back(std::move(arg));
        }

        def.nodes[i] = std::move(n);
    }

    validate_definition(def);
    return def;
}

void export_definition_dot(const definition& def, const std::string& path) {
    validate_definition(def);

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to open file: " + path);
    }

    out << "digraph bt {\n";
    out << "  rankdir=TB;\n";
    out << "  node [shape=box, fontname=\"Helvetica\"];\n";
    out << "  edge [fontname=\"Helvetica\"];\n";

    for (const node& n : def.nodes) {
        out << "  n" << n.id << " [label=\"" << dot_escape(node_label(n)) << "\"";
        if (n.id == def.root) {
            out << ", style=\"bold\"";
        }
        out << "];\n";
    }

    for (const node& n : def.nodes) {
        for (const node_id child : n.children) {
            out << "  n" << n.id << " -> n" << child << ";\n";
        }
    }

    out << "}\n";
    if (!out) {
        throw std::runtime_error("failed while writing file: " + path);
    }
}

}  // namespace bt
