/** Copyright (c) 2022 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/
#ifndef MUESLIP_PAIR_HPP
#define MUESLIP_PAIR_HPP

#include "nil.hpp"

namespace muesli
{
class Pair;
using PairPtr = std::shared_ptr<Pair>;    

class Pair : public Cell 
{
public:
    Pair(const CellPtr& car = nil, const CellPtr& cdr = nil) :
	m_car(car), m_cdr(cdr) {}

    inline CellPtr getCar() const
    { return m_car; }

    inline CellPtr getCdr() const
    { return m_cdr; }

    inline void setCar(CellPtr car)
    { m_car = car; }

    inline void setCdr(CellPtr cdr)
    { m_cdr = cdr; }
    
    std::string toString() const override
    { return toString(true); }

    bool isAtom() const override
    { return false; }

    std::string toString(bool parens) const;

private:
    CellPtr m_car;
    CellPtr m_cdr;
};

}

#endif // MUESLISP_PAIR_HPP
