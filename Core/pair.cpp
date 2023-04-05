/** Copyright (c) 2022 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/
#include "pair.hpp"

using namespace muesli;

std::string
Pair::toString(bool parens) const
{
    std::string s = parens ? "(" : "";

    auto cdr_atom = std::dynamic_pointer_cast<Atom>(m_cdr);

    s += m_car->toString();

    if (cdr_atom)
    {
	if (!cdr_atom->isNIL())
	{
	    s += " . " + cdr_atom->toString();
	}
    }
    else
    {
	s += " " + std::dynamic_pointer_cast<Pair>(m_cdr)->toString(false);
    }
	
    if (parens)
	s += ')';
    return s;
}
