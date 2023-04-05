/** Copyright (c) 2022 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/
#ifndef MUESLISP_INTEGER_HPP
#define MUESLISP_INTEGER_HPP

#include "atom.hpp"

namespace muesli
{
class Integer;    
using IntegerPtr = std::shared_ptr<Integer>;
    
class Integer : public Atom
{
public:
    explicit Integer(int value) : Atom(), m_value(value)
    {}

    int value() const
    { return m_value; }

    std::string toString() const override
    { return std::to_string(m_value); }

    static IntegerPtr create(int value);

    bool operator <(const Integer& i) const;

private:
    int m_value;
};

    
}

inline bool
muesli::Integer::operator <(const muesli::Integer& i) const
{
    return value() < i.value();
}

#endif // MUESLISP_INTEGER_HPP
