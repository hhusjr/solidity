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
 * @author Federico Bond <federicobond@gmail.com>
 * @date 2016
 * Static analyzer and checker.
 */

#pragma once

#include <libsolidity/analysis/TypeChecker.h>
#include <libsolidity/ast/Types.h>
#include <libsolidity/ast/ASTAnnotations.h>
#include <libsolidity/ast/ASTForward.h>
#include <libsolidity/ast/ASTVisitor.h>
#include <libsolidity/analysis/ConstructorChecker.h>

namespace langutil
{
class ErrorReporter;
}

namespace dev
{
namespace solidity
{

class ConstructorUsesAssembly;

struct FunctionFeature {
	FunctionDefinition const* function;

	int nAssignment = 0;
	int nNormalAssignment = 0;

	int nStateVarAssignment = 0;
	int nStateVarNormalAssignment = 0;

	int nRightHandSideMsgSender = 0;
	int nOwnerIsMsgSender = 0;

	int nLeftHandSideBalanceAssign = 0;
	int nLeftHandSideBalanceOwnerAssign = 0;

	int nIf = 0;
	int nLoop = 0;
	int nEmit = 0;
	int nVarDefinition = 0;
	int nGuarantee = 0;
	int nFunctionCall = 0;

	int visibilityTypeCode = 0;
	bool isOwnerOnly = false;
	bool hasReturnValue = false;
	bool isCalled = false;

	void print() {
		std::cout << "===== Function report: " << function->name() << " =====" << std::endl;
		std::cout << "nAssignment=" << nAssignment << std::endl;
		std::cout << "nNormalAssignment=" << nNormalAssignment << std::endl;
		std::cout << "nStateVarAssignment=" << nStateVarAssignment << std::endl;
		std::cout << "nStateVarNormalAssignment=" << nStateVarNormalAssignment << std::endl;
		std::cout << "nRightHandSideMsgSender=" << nRightHandSideMsgSender << std::endl;
		std::cout << "nOwnerIsMsgSender=" << nOwnerIsMsgSender << std::endl;
		std::cout << "nLeftHandSideBalanceAssign=" << nLeftHandSideBalanceAssign << std::endl;
		std::cout << "nLeftHandSideBalanceOwnerAssign=" << nLeftHandSideBalanceOwnerAssign << std::endl;
		std::cout << "nIf=" << nIf << std::endl;
		std::cout << "nLoop=" << nLoop << std::endl;
		std::cout << "nEmit=" << nEmit << std::endl;
		std::cout << "nVarDefinition=" << nVarDefinition << std::endl;
		std::cout << "nGuarantee=" << nGuarantee << std::endl;
		std::cout << "nFunctionCall=" << nFunctionCall << std::endl;
		std::cout << "visibilityTypeCode=" << visibilityTypeCode << std::endl;
		std::cout << "isOwnerOnly=" << isOwnerOnly << std::endl;
		std::cout << "hasReturnValue=" << hasReturnValue << std::endl;
		std::cout << "isCalled=" << isCalled << std::endl;
		std::cout << "predicted=" << ConstructorChecker(toFeatures()) << std::endl;
	}

	std::vector<double> toFeatures() {
		std::vector<double> featureVector;
		featureVector.resize(19);
		/*
		feature_vector[0] - loop
		feature_vector[1] - rhs_msgsender
		feature_vector[2] - var_definition
		feature_vector[3] - is_only_owner
		feature_vector[4] - state_var_normal_assignment
		feature_vector[5] - is_called
		feature_vector[6] - owner_is_msgsender
		feature_vector[7] - guarantee
		feature_vector[8] - is_return
		feature_vector[9] - assignment
		feature_vector[10] - total_stmt
		feature_vector[11] - state_var_assign
		feature_vector[12] - emit
		feature_vector[13] - visibility
		feature_vector[14] - normal_assignment
		feature_vector[15] - if
		feature_vector[16] - function_call
		feature_vector[17] - lhs_balance_owner_assign
		feature_vector[18] - lhs_balance_assign
		 */
		double totalStmt = nAssignment + nIf + nLoop + nVarDefinition + nEmit + nGuarantee + nFunctionCall;
		featureVector[0] = nLoop / totalStmt;
		featureVector[1] = nRightHandSideMsgSender / totalStmt;
		featureVector[2] = nVarDefinition / totalStmt;
		featureVector[3] = isOwnerOnly;
		featureVector[4] = nStateVarNormalAssignment / totalStmt;
		featureVector[5] = isCalled;
		featureVector[6] = nOwnerIsMsgSender / totalStmt;
		featureVector[7] = nGuarantee / totalStmt;
		featureVector[8] = hasReturnValue;
		featureVector[9] = nAssignment / totalStmt;
		featureVector[10] = totalStmt;
		featureVector[11] = nStateVarAssignment / totalStmt;
		featureVector[12] = nEmit / totalStmt;
		featureVector[13] = visibilityTypeCode;
		featureVector[14] = nNormalAssignment / totalStmt;
		featureVector[15] = nIf / totalStmt;
		featureVector[16] = nFunctionCall / totalStmt;
		featureVector[17] = nLeftHandSideBalanceOwnerAssign / totalStmt;
		featureVector[18] = nLeftHandSideBalanceAssign / totalStmt;
		return featureVector;
	}
};

/**
 * The module that performs static analysis on the AST.
 * In this context, static analysis is anything that can produce warnings which can help
 * programmers write cleaner code. For every warning generated here, it has to be possible to write
 * equivalent code that does not generate the warning.
 */
class StaticAnalyzer: private ASTConstVisitor
{
public:
	/// @param _errorReporter provides the error logging functionality.
	explicit StaticAnalyzer(langutil::ErrorReporter& _errorReporter);
	~StaticAnalyzer();

	/// Performs static analysis on the given source unit and all of its sub-nodes.
	/// @returns true iff all checks passed. Note even if all checks passed, errors() can still contain warnings
	bool analyze(SourceUnit const& _sourceUnit);

private:
    static const uint8_t DISTANCE_THRESHOLD = 2;

	bool visit(ContractDefinition const& _contract) override;
	void endVisit(ContractDefinition const& _contract) override;

	bool visit(FunctionDefinition const& _function) override;
	void endVisit(FunctionDefinition const& _function) override;

	bool visit(ExpressionStatement const& _statement) override;
	bool visit(IfStatement const& _statement) override;
	bool visit(ForStatement const& _statement) override;
	bool visit(WhileStatement const& _statement) override;
	bool visit(VariableDeclaration const& _variable) override;
	bool visit(VariableDeclarationStatement const& _statement) override;
	bool visit(EmitStatement const& _statement) override;
	bool visit(Identifier const& _identifier) override;
	bool visit(Return const& _return) override;
	bool visit(MemberAccess const& _memberAccess) override;
	bool visit(InlineAssembly const& _inlineAssembly) override;
	bool visit(BinaryOperation const& _operation) override;
	bool visit(FunctionCall const& _functionCall) override;

	/// @returns the size of this type in storage, including all sub-types.
	static bigint structureSizeEstimate(Type const& _type, std::set<StructDefinition const*>& _structsSeen);

	/// @returns the edit distance between two strings
	static size_t stringDistance(const ASTString &a, const ASTString &b);

	/// @returns a flag indicates if the MethodAccess is msg.sender
	static bool isMsgSender(const MemberAccess *methodAccess);

	langutil::ErrorReporter& m_errorReporter;

	/// Flag that indicates whether the current contract definition is a library.
	bool m_library = false;

	/// Number of uses of each (named) local variable in a function, counter is initialized with zero.
	/// Pairs of AST ids and pointers are used as keys to ensure a deterministic order
	/// when traversing.
	std::map<std::pair<size_t, VariableDeclaration const*>, int> m_localVarUseCount;

	/// Cache that holds information about whether a contract's constructor
	/// uses inline assembly.
	std::unique_ptr<ConstructorUsesAssembly> m_constructorUsesAssembly;

	FunctionDefinition const* m_currentFunction = nullptr;

	/// Flag that indicates a constructor.
	bool m_constructor = false;

	// Flag that indicates if current contract has a constructor
	bool m_hasConstructor = false;

	/// Current contract.
	ContractDefinition const* m_currentContract = nullptr;

	/// Function names that are similiar with contract name and satisfy the decision tree
	std::vector<FunctionFeature> m_mayBeConstructor;

	// Current function need to be analyzed?
	bool m_currentFunctionRequiresAnalysis = false;

	// Features of current visiting function
	FunctionFeature* m_currentFunctionFeatures;

	// Called function names
	std::set<ASTString> m_calledFunctionNames;

	static Identifier const* leftHandSideMainPart(Expression const* lhs);

	static ASTString toLowerCase(const ASTString &str);
};

}
}
