/** Copyright (c) 2022 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/
#ifndef MUESLIP_ATOM_HPP
#define MUESLIP_ATOM_HPP

#include "cell.hpp"

namespace muesli
{

class Atom;
using AtomPtr = std::shared_ptr<Atom>;

class Atom : public Cell
{
public:
    bool isAtom() const override
    { return true; }

    static AtomPtr create(const std::string& s);

    static AtomPtr create(int value);

    static AtomPtr create(double value);

protected:
    Atom() = default;
};

}

#endif // MUESLISP_ATOM_HPP
