/** Copyright (c) 2022 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/
#ifndef MUESLISP_DFLOAT_HPP
#define MUESLISP_DFLOAT_HPP

#include "atom.hpp"

namespace muesli
{
class DFloat;    
using DFloatPtr = std::shared_ptr<DFloat>;
    
class DFloat : public Atom
{
public:
    explicit DFloat(double value) : Atom(), m_value(value)
    {}

    double value() const
    { return m_value; }

    std::string toString() const override
    { return std::to_string(m_value); }

    static DFloatPtr create(double value);

    bool operator <(const DFloat& i) const;

private:
    double m_value;
};

    
}

inline bool
muesli::DFloat::operator <(const muesli::DFloat& d) const
{
    return value() < d.value();
}

#endif // MUESLISP_DFLOAT_HPP
