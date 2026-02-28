#include "muslisp/printer.hpp"

#include <cmath>
#include <iomanip>
#include <ios>
#include <sstream>
#include <string_view>

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

std::string print_impl(value v, bool readable);

void append_list(std::ostringstream& out, value list_value, bool readable) {
    out << "(";
    value cursor = list_value;
    bool first = true;

    while (is_cons(cursor)) {
        if (!first) {
            out << " ";
        }
        out << print_impl(car(cursor), readable);
        cursor = cdr(cursor);
        first = false;
    }

    if (!is_nil(cursor)) {
        if (!first) {
            out << " ";
        }
        out << ". " << print_impl(cursor, readable);
    }

    out << ")";
}

std::string write_error_message(value_type t) {
    return "write: cannot serialise " + std::string(type_name(t)) + " as readable data";
}

std::string print_impl(value v, bool readable) {
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
            if (readable && !std::isfinite(float_value(v))) {
                throw lisp_error("write: non-finite floats are not readable");
            }
            return format_float(float_value(v));
        case value_type::symbol:
            return symbol_name(v);
        case value_type::string:
            return "\"" + escape_string(string_value(v)) + "\"";
        case value_type::cons: {
            std::ostringstream out;
            append_list(out, v, readable);
            return out.str();
        }
        case value_type::primitive_fn:
            if (readable) {
                throw lisp_error(write_error_message(value_type::primitive_fn));
            }
            return "<primitive:" + primitive_name(v) + ">";
        case value_type::closure:
            if (readable) {
                throw lisp_error(write_error_message(value_type::closure));
            }
            return "<closure>";
        case value_type::vec:
            if (readable) {
                throw lisp_error(write_error_message(value_type::vec));
            }
            return "<vec:" + std::to_string(v->vec_data.size()) + ">";
        case value_type::map:
            if (readable) {
                throw lisp_error(write_error_message(value_type::map));
            }
            return "<map:" + std::to_string(v->map_data.size()) + ">";
        case value_type::pq:
            if (readable) {
                throw lisp_error(write_error_message(value_type::pq));
            }
            return "<pq:" + std::to_string(v->pq_data.size()) + ">";
        case value_type::rng:
            if (readable) {
                throw lisp_error(write_error_message(value_type::rng));
            }
            return "<rng>";
        case value_type::bt_def:
            if (readable) {
                throw lisp_error(write_error_message(value_type::bt_def));
            }
            return "<bt_def:" + std::to_string(bt_handle(v)) + ">";
        case value_type::bt_instance:
            if (readable) {
                throw lisp_error(write_error_message(value_type::bt_instance));
            }
            return "<bt_instance:" + std::to_string(bt_handle(v)) + ">";
        case value_type::image_handle:
            if (readable) {
                throw lisp_error(write_error_message(value_type::image_handle));
            }
            return "<image_handle:" + std::to_string(image_handle_id(v)) + ">";
        case value_type::blob_handle:
            if (readable) {
                throw lisp_error(write_error_message(value_type::blob_handle));
            }
            return "<blob_handle:" + std::to_string(blob_handle_id(v)) + ">";
    }

    return "<unknown>";
}

}  // namespace

std::string print_value(value v) {
    return print_impl(v, false);
}

std::string write_value(value v) {
    return print_impl(v, true);
}

}  // namespace muslisp
