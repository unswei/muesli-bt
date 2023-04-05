/** Copyright (c) 2023 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/
#ifndef MUESLISP_TOKEN_HPP
#define MUESLISP_TOKEN_HPP

#include "atom.hpp"
#include <string>

namespace muesli
{

class Token
{
public:
    enum class Type
    {
	T_STRING,
	T_SYMBOL,
	T_NUMBER,
	T_LP,
	T_RP,
	T_INVALID
    };
    Token(Token::Type t);
    
    Token(Token::Type t, const std::string& s);
    
    int type() const;

    const std::string& string() const;

    AtomPtr value();

    static void ltrim(std::string& s);

    static void rtrim(std::string& s);
    
private:
    Type m_type;

    unsigned char m_c;

    AtomPtr m_valuep;
};

std::string toString(Token::Type t);

}

#endif // MUESLISP_TOKEN_HPP
