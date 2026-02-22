#include "muslisp/printer.hpp"

#include <cmath>
#include <iomanip>
#include <ios>
#include <sstream>

#include "muslisp/error.hpp"

namespace muslisp {
namespace {

std::string escape_string(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
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
            case '\t':
                out += "\\t";
                break;
            case '\r':
                out += "\\r";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string format_float(double v) {
    if (std::isnan(v)) {
        return "nan";
    }
    if (std::isinf(v)) {
        return v < 0.0 ? "-inf" : "inf";
    }

    std::ostringstream out;
    out.setf(std::ios::fmtflags(0), std::ios::floatfield);
    out << std::setprecision(15) << v;
    std::string text = out.str();

    if (text.find('.') == std::string::npos && text.find('e') == std::string::npos &&
        text.find('E') == std::string::npos) {
        text += ".0";
    }
    return text;
}

void append_list(std::ostringstream& out, value list_value) {
    out << "(";
    value cursor = list_value;
    bool first = true;

    while (is_cons(cursor)) {
        if (!first) {
            out << " ";
        }
        out << print_value(car(cursor));
        cursor = cdr(cursor);
        first = false;
    }

    if (!is_nil(cursor)) {
        if (!first) {
            out << " ";
        }
        out << ". " << print_value(cursor);
    }

    out << ")";
}

}  // namespace

std::string print_value(value v) {
    if (!v) {
        throw lisp_error("print_value: null value");
    }

    switch (type_of(v)) {
        case value_type::nil:
            return "nil";
        case value_type::boolean:
            return boolean_value(v) ? "#t" : "#f";
        case value_type::integer:
            return std::to_string(integer_value(v));
        case value_type::floating:
            return format_float(float_value(v));
        case value_type::symbol:
            return symbol_name(v);
        case value_type::string:
            return "\"" + escape_string(string_value(v)) + "\"";
        case value_type::cons: {
            std::ostringstream out;
            append_list(out, v);
            return out.str();
        }
        case value_type::primitive_fn:
            return "<primitive:" + primitive_name(v) + ">";
        case value_type::closure:
            return "<closure>";
        case value_type::bt_def:
            return "<bt_def:" + std::to_string(bt_handle(v)) + ">";
        case value_type::bt_instance:
            return "<bt_instance:" + std::to_string(bt_handle(v)) + ">";
    }

    return "<unknown>";
}

}  // namespace muslisp
