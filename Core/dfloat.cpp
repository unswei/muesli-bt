/** Copyright (c) 2022 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/

#include "dfloat.hpp"

using namespace muesli;

DFloatPtr
DFloat::create(double value)
{
    return std::make_shared<DFloat>(value);
}
