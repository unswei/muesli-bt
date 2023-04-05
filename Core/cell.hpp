/** Copyright (c) 2023 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/
#ifndef MUESLISP_CELL_HPP
#define MUESLISP_CELL_HPP

#include <memory>
#include <string>

namespace muesli
{

class Cell;
using CellPtr = std::shared_ptr<Cell>;

class Cell
{
public:
    virtual ~Cell() = default;

    virtual bool isNIL() const
    { return false; }

    virtual std::string toString() const = 0;

    virtual bool isAtom() const = 0;
};

}

#endif // MUESLISP_CELL_HPP
