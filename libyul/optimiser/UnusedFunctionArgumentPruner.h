/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * UnusedFunctionArgumentPruner: Optimiser step that removes unused-parameters in a function.
 *
 * If a parameter is unused, like in, `function f(a,b,c) -> x { x := div(a,b) }`
 *
 * We remove the parameter and create a new "linking" function as follows:
 *
 * `function f(a,b) -> x { x := div(a,b) }`
 * `function f2(a,b,c) -> x { x := f(a,b) }`
 *
 * and replace all references to `f` by `f2`.
 *
 * Prerequisites: Disambiguator, FunctionHoister
 *
 * The inliner should be run afterwards to make sure that all references to `f2` are replaced by
 * `f`.
 */

#pragma once

#include <libyul/optimiser/OptimiserStep.h>

namespace solidity::yul
{

struct UnusedFunctionArgumentPruner
{
	static constexpr char const* name{"UnusedFunctionArgumentPruner"};
	static void run(OptimiserStepContext& _context, Block& _ast);
};

}
