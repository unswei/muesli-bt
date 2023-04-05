/** Copyright (c) 2022 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/
#ifndef MUESLISP_STRING_HPP
#define MUESLISP_STRING_HPP

#include "atom.hpp"

namespace muesli
{
class String;    
using StringPtr = std::shared_ptr<String>;
    
class String : public Atom
{
public:
    explicit String(const std::string& value) : Atom(), m_value(value)
    {}

    const std::string& value() const;

    std::string toString() const override
    { return "\"" + m_value + "\""; }

    static StringPtr create(const std::string& s);

    bool operator <(const String& s) const;

private:
    std::string m_value;
};

    
}

inline bool
muesli::String::operator <(const muesli::String& s) const
{
    return value() < s.value();
}

#endif // MUESLISP_STRING_HPP
