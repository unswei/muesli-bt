/** Copyright (c) 2022 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/

#include "string.hpp"

using namespace muesli;

const std::string&
String::value() const
{
    return m_value;
}

StringPtr
String::create(const std::string& s)
{
    return std::make_shared<String>(s);
}
