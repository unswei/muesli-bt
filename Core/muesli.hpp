/** Copyright (c) 2023 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/
#ifndef MUESLISP_MUESLI_HPP
#define MUESLISP_MUESLI_HPP

#include "cell.hpp"
#include "token.hpp"

#include <list>
#include <string>

namespace muesli
{

class Muesli
{
public:
    Muesli() = default;

    std::list<std::string> tokenise(const std::string& input);
    
    int repl();

    
    
private:
    static std::list<std::string> split(const std::string& s);

    static CellPtr readFromTokens(std::list<std::string>& tokens);    
};

}

#endif // MUESLISP_MUESLI_HPP
