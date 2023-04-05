/** Copyright (c) 2023 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/
#include "muesli.hpp"
#include "muexcept.hpp"
#include "nil.hpp"
#include "pair.hpp"

#include <algorithm>
#include <iostream>
#include <list>
#include <sstream>
#include <string>

using namespace muesli;

std::list<std::string>
Muesli::tokenise(const std::string& input)
{
    std::list<std::string> tokens;
    std::string s;
    Token::Type t = Token::Type::T_INVALID;

    for (auto c : input)
    {
	if (c == '(' || c == ')')
	{
	    s += " " + std::string(1, c) + " ";
	}
	else
	{
	    s += c;
	}
    }

    tokens = Muesli::split(s);

    return tokens;
}

CellPtr
Muesli::readFromTokens(std::list<std::string>& tokens)
{
    if (tokens.empty()) throw SyntaxError();
    
    std::string t = tokens.front();
    tokens.pop_front();

    if (t == "(")
    {
	if (tokens.empty()) throw SyntaxError();
	
	CellPtr p = nil;
	PairPtr q;
	while (!tokens.empty() && tokens.front() != ")")
	{
	    if (p->isNIL())
	    {
		q = std::make_shared<Pair>(readFromTokens(tokens));
		p = std::static_pointer_cast<Cell>(q);
	    }
	    else
	    {
		q->setCdr(std::make_shared<Pair>(readFromTokens(tokens)));
		q = std::static_pointer_cast<Pair>(q->getCdr());
	    }
	}
	if (tokens.empty()) throw SyntaxError();
	tokens.pop_front(); // remove ')'
	return p;
    }
    else if (t == ")")
    {
	throw SyntaxError("unexpected ')'");
    }
    else return Atom::create(t); // creates string atom
}

int
Muesli::repl()
{
    std::string input;
    std::list<std::string> tlist;
    
    CellPtr tokens;
    
    while (true)
    {
	std::cout << "> ";
	std::getline(std::cin, input);
	if (input == "(quit)" || input == "exit")
	{
	    break;
	}
	tlist = tokenise(input);
	tokens = Muesli::readFromTokens(tlist);
	std::cout << tokens->toString() << std::endl;
    }

    return 0;
}


std::list<std::string>
Muesli::split(const std::string& s)
{
    std::list<std::string> words;
    std::istringstream iss(s);
    std::string w;
    while (iss >> w)
    {
	words.push_back(w);
    }
    return words;
}
