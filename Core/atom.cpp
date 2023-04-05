/** Copyright (c) 2022 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/

#include "atom.hpp"
#include "dfloat.hpp"
#include "integer.hpp"
#include "string.hpp"
#include "symbol.hpp"

using namespace muesli;

AtomPtr
Atom::create(const std::string& s)
{
    // we create either a string or symbol.
    if (s.size() > 0 && s[0] == '\'')
    {
	// get rid of the quote and create the symbol as requested
	return Symbol::create(s.substr(1));
    }
    else
    {
	// we create a string object
	return String::create(s);
    }
}

AtomPtr
Atom::create(int value)
{
    return Integer::create(value);
}

AtomPtr
Atom::create(double value)
{
    return DFloat::create(value);
}
