// Copyright 2017 James Bendig. See the COPYRIGHT file at the top-level
// directory of this distribution.
//
// Licensed under:
//   the MIT license
//     <LICENSE-MIT or https://opensource.org/licenses/MIT>
//   or the Apache License, Version 2.0
//     <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>,
// at your option. This file may not be copied, modified, or distributed
// except according to those terms.

#include "CachedPuzzleSolver.h"
#include <algorithm>
#include <numeric>
#include "Game.h"
#include "Solve.h"


static Game DigitsToGame(const std::vector<unsigned char>& digits)
{
	Game game;
	for(unsigned int y = 0;y < 9;y++)
	{
		for(unsigned int x = 0;x < 9;x++)
		{
			const unsigned int index = y * 9 + x;
			game.Set(x,y,digits[index]);
		}
	}

	return game;
}

CachedPuzzleSolver::CachedPuzzleSolver()
	: solvedPuzzles(),
	  lastUsedSolution(solvedPuzzles.end())
{
}

bool CachedPuzzleSolver::Solve(const std::vector<unsigned char>& digits,std::vector<unsigned char>& solution)
{
	Game game = DigitsToGame(digits);

	//Is this a valid puzzle?
	if(digits.size() != 81)
		return false;
	else if(std::any_of(digits.begin(),digits.end(),[](const unsigned char digit) {
		return digit > 9;
	}))
		return false;
	else if(!Solvable(game))
		return false;

	//Does the puzzle have a solution that can be found in a reasonable amount of time?
	const unsigned int nonZeroDigits = std::accumulate(digits.begin(),digits.end(),0,[](const unsigned char previous,const unsigned char digit) {
		return previous + (digit > 0);
	});
	if(nonZeroDigits < 21)
		return false;

	//Has this puzzle already been solved once? Use the previous solution.
	auto iter = solvedPuzzles.find(digits);
	if(iter != solvedPuzzles.end())
	{
		solution = iter->second;
		lastUsedSolution = iter;
		return true;
	}

	//Solve the puzzle.
	if(!::Solve(game))
		return false;

	//Cache solution to save time in the future.
	solution.clear();
	for(unsigned int y = 0;y < 9;y++)
	{
		for(unsigned int x = 0;x < 9;x++)
		{
			const unsigned char digit = game.Get(x,y);
			solution.push_back(digit);
		}
	}
	lastUsedSolution = solvedPuzzles.emplace(digits,solution).first;

	return true;
}

bool CachedPuzzleSolver::GetLastUsedSolution(std::vector<unsigned char>& solution) const
{
	if(lastUsedSolution == solvedPuzzles.end())
		return false;

	solution = lastUsedSolution->second;
	return true;
}
