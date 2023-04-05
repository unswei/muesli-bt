/** Copyright (c) 2022 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/

#include "integer.hpp"

using namespace muesli;

IntegerPtr
Integer::create(int value)
{
    return std::make_shared<Integer>(value);
}
