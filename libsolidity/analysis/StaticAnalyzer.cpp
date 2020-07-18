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

#include <libsolidity/analysis/StaticAnalyzer.h>

#include <libsolidity/analysis/ConstantEvaluator.h>
#include <libsolidity/ast/AST.h>
#include <liblangutil/ErrorReporter.h>
#include <memory>

using namespace std;
using namespace dev;
using namespace langutil;
using namespace dev::solidity;

/**
 * Helper class that determines whether a contract's constructor uses inline assembly.
 */
class dev::solidity::ConstructorUsesAssembly
{
public:
	/// @returns true if and only if the contract's or any of its bases' constructors
	/// use inline assembly.
	bool check(ContractDefinition const& _contract)
	{
		for (auto const* base: _contract.annotation().linearizedBaseContracts)
			if (checkInternal(*base))
				return true;
		return false;
	}


private:
	class Checker: public ASTConstVisitor
	{
	public:
		Checker(FunctionDefinition const& _f) { _f.accept(*this); }
		bool visit(InlineAssembly const&) override { assemblySeen = true; return false; }
		bool assemblySeen = false;
	};

	bool checkInternal(ContractDefinition const& _contract)
	{
		if (!m_usesAssembly.count(&_contract))
		{
			bool usesAssembly = false;
			if (_contract.constructor())
				usesAssembly = Checker{*_contract.constructor()}.assemblySeen;
			m_usesAssembly[&_contract] = usesAssembly;
		}
		return m_usesAssembly[&_contract];
	}

	map<ContractDefinition const*, bool> m_usesAssembly;
};

StaticAnalyzer::StaticAnalyzer(ErrorReporter& _errorReporter):
	m_errorReporter(_errorReporter)
{
}

StaticAnalyzer::~StaticAnalyzer()
{
}

bool StaticAnalyzer::analyze(SourceUnit const& _sourceUnit)
{
	_sourceUnit.accept(*this);
	return Error::containsOnlyWarnings(m_errorReporter.errors());
}

bool StaticAnalyzer::visit(ContractDefinition const& _contract)
{
	m_library = _contract.isLibrary();
	m_currentContract = &_contract;
	m_hasConstructor = false;
	m_mayBeConstructor.clear();
	m_calledFunctionNames.clear();
	m_currentFunctionFeatures = nullptr;
	return true;
}

void StaticAnalyzer::endVisit(ContractDefinition const&)
{
	m_library = false;
	m_currentContract = nullptr;

	// check function names
	if (m_hasConstructor) {
        return;
	}

	for (auto function : m_mayBeConstructor) {
		function.isCalled = m_calledFunctionNames.count(function.function->name());
		if (ConstructorChecker(function.toFeatures())) {
			m_errorReporter.warning(
				function.function->location(),
				"The function name is similar with the contract name but no constructor has been found "
				"in the contract right now. Maybe it's a constructor?"
			);
		}
	}
}

bool StaticAnalyzer::visit(FunctionDefinition const& _function)
{
	if (_function.isImplemented())
		m_currentFunction = &_function;
	else
		solAssert(!m_currentFunction, "");
	solAssert(m_localVarUseCount.empty(), "");
	m_constructor = _function.isConstructor();

	if (m_constructor) {
	    m_hasConstructor = true;
	}

	if (m_hasConstructor) {
	    return true;
	}

	// check function name
	const ASTString& name = _function.name();
	if (name.empty()) {
		return true;
	}
	m_currentFunctionRequiresAnalysis = false;
	size_t dis = stringDistance(name, m_currentContract->name());
	if (dis <= DISTANCE_THRESHOLD) {
        m_mayBeConstructor.emplace_back();
        m_currentFunctionFeatures = &m_mayBeConstructor[m_mayBeConstructor.size() - 1];
        m_currentFunctionRequiresAnalysis = true;

        m_currentFunctionFeatures->function = &_function;

        for (auto modifier : _function.modifiers()) {
        	if (toLowerCase(modifier->name()->name()).find("owner") != std::string::npos) {
				m_currentFunctionFeatures->isOwnerOnly = true;
				break;
        	}
        }
        m_currentFunctionFeatures->hasReturnValue = !_function.returnParameters().empty();

        switch (_function.visibility()) {
			case Declaration::Visibility::Default:
				m_currentFunctionFeatures->visibilityTypeCode = 1;
				break;
			case Declaration::Visibility::Private:
				m_currentFunctionFeatures->visibilityTypeCode = 4;
				break;
			case Declaration::Visibility::Internal:
				m_currentFunctionFeatures->visibilityTypeCode = 2;
				break;
			case Declaration::Visibility::Public:
				m_currentFunctionFeatures->visibilityTypeCode = 1;
				break;
			case Declaration::Visibility::External:
				m_currentFunctionFeatures->visibilityTypeCode = 3;
				break;
		}
	}

	return true;
}

void StaticAnalyzer::endVisit(FunctionDefinition const&)
{
	if (m_currentFunction && !m_currentFunction->body().statements().empty())
		for (auto const& var: m_localVarUseCount)
			if (var.second == 0)
			{
				if (var.first.second->isCallableParameter())
					m_errorReporter.warning(
						var.first.second->location(),
						"Unused function parameter. Remove or comment out the variable name to silence this warning."
					);
				else
					m_errorReporter.warning(var.first.second->location(), "Unused local variable.");
			}
	m_localVarUseCount.clear();
	m_constructor = false;
	m_currentFunction = nullptr;

	m_currentFunctionRequiresAnalysis = false;
}

bool StaticAnalyzer::visit(Identifier const& _identifier)
{
	if (m_currentFunction)
		if (auto var = dynamic_cast<VariableDeclaration const*>(_identifier.annotation().referencedDeclaration))
		{
			solAssert(!var->name().empty(), "");
			if (var->isLocalVariable())
				m_localVarUseCount[make_pair(var->id(), var)] += 1;
		}
	return true;
}

bool StaticAnalyzer::visit(VariableDeclaration const& _variable)
{
	if (m_currentFunction)
	{
		solAssert(_variable.isLocalVariable(), "");
		if (_variable.name() != "")
			// This is not a no-op, the entry might pre-exist.
			m_localVarUseCount[make_pair(_variable.id(), &_variable)] += 0;
	}
	else if (_variable.isStateVariable())
	{
		set<StructDefinition const*> structsSeen;
		if (structureSizeEstimate(*_variable.type(), structsSeen) >= bigint(1) << 64)
			m_errorReporter.warning(
				_variable.location(),
				"Variable covers a large part of storage and thus makes collisions likely. "
				"Either use mappings or dynamic arrays and allow their size to be increased only "
				"in small quantities per transaction."
			);
	}
	return true;
}

bool StaticAnalyzer::visit(Return const& _return)
{
	// If the return has an expression, it counts as
	// a "use" of the return parameters.
	if (m_currentFunction && _return.expression())
		for (auto const& var: m_currentFunction->returnParameters())
			if (!var->name().empty())
				m_localVarUseCount[make_pair(var->id(), var.get())] += 1;
	return true;
}

Identifier const* StaticAnalyzer::leftHandSideMainPart(Expression const* lhs) {
	Expression const* expr = lhs;
	// remove the member link
	while (const MemberAccess* member = dynamic_cast<MemberAccess const*>(expr)) {
		expr = &member->expression();
	}
	while (const IndexAccess* indexAccess = dynamic_cast<IndexAccess const*>(expr)) {
		expr = &indexAccess->baseExpression();
	}
	while (const MemberAccess* member = dynamic_cast<MemberAccess const*>(expr)) {
		expr = &member->expression();
	}
	return dynamic_cast<Identifier const*>(expr);
}

bool StaticAnalyzer::visit(ExpressionStatement const& _statement)
{
	if (_statement.expression().annotation().isPure)
		m_errorReporter.warning(
			_statement.location(),
			"Statement has no effect."
		);

	if (!m_currentFunctionRequiresAnalysis) {
		return true;
	}

	if (const FunctionCall* functionCall = dynamic_cast<const FunctionCall*>(&_statement.expression())) {
		if (const Identifier* funcId = dynamic_cast<const Identifier*>(&functionCall->expression())) {
			if (funcId->name().empty()) {
				return true;
			}
			if (funcId->name() == "require"
			||  funcId->name() == "assert"
			||  funcId->name() == "revert") {
				m_currentFunctionFeatures->nGuarantee++;
				return true;
			}
			m_calledFunctionNames.insert(funcId->name());
		}
		return true;
	}

	if (const Assignment* assignment = dynamic_cast<const Assignment*>(&_statement.expression())) {
		m_currentFunctionFeatures->nAssignment++;
		if (assignment->assignmentOperator() == Token::Assign) {
			m_currentFunctionFeatures->nNormalAssignment++;
		}

		std::set<std::string> stateVariableNames;
		for (auto &var : m_currentContract->stateVariables()) {
			stateVariableNames.insert(var->name());
		}

		bool isStateVar = false;

		if (const IndexAccess *lhsIndexAccess = dynamic_cast<const IndexAccess *>(&assignment->leftHandSide())) {
			// a[x]
			if (const Identifier *lhsIndexAccessVarName = dynamic_cast<const Identifier *>(&lhsIndexAccess->baseExpression())) {
				// state var
				if (stateVariableNames.count(lhsIndexAccessVarName->name())) {
					isStateVar = true;
				}

				// balance[x] = y
				if (toLowerCase(lhsIndexAccessVarName->name()).find("balance") != std::string::npos) {
					m_currentFunctionFeatures->nLeftHandSideBalanceAssign++;

					if (const Identifier *idInIndex = dynamic_cast<const Identifier *>(lhsIndexAccess->indexExpression())) {
						// balance[owner] = x
						if (toLowerCase(idInIndex->name()).find("owner") != std::string::npos) {
							m_currentFunctionFeatures->nLeftHandSideBalanceOwnerAssign++;
						}
					} else if (const MemberAccess *memberInIndex = dynamic_cast<const MemberAccess *>(lhsIndexAccess->indexExpression())) {
						// balance[msg.sender] = x
						if (isMsgSender(memberInIndex)) {
							m_currentFunctionFeatures->nLeftHandSideBalanceOwnerAssign++;
						}
					}
				}
			}
		} else if (Identifier const * id = leftHandSideMainPart(&assignment->leftHandSide())) {
			if (stateVariableNames.count(id->name())) {
				isStateVar = true;
			}
		}

		if (isStateVar) {
			m_currentFunctionFeatures->nStateVarAssignment++;
			if (assignment->assignmentOperator() == Token::Assign) {
				m_currentFunctionFeatures->nStateVarNormalAssignment++;
			}
		}

		// x = msg.sender
		if (const MemberAccess *rhsMemberAccess = dynamic_cast<const MemberAccess *>(&assignment->rightHandSide())) {
			m_currentFunctionFeatures->nRightHandSideMsgSender++;
			if (isMsgSender(rhsMemberAccess)) {
				// owner = msg.sender
				if (const MemberAccess *lhsMemberAccess = dynamic_cast<const MemberAccess *>(&assignment->leftHandSide())) {
					if (const Identifier *lhsMemberAccessVarName = dynamic_cast<const Identifier *>(&lhsMemberAccess->expression())) {
						if (lhsMemberAccessVarName->name() == "owner") {
							m_currentFunctionFeatures->nOwnerIsMsgSender++;
						}
					}
				}
			}
		}

		return true;
	}

	return true;
}

bool StaticAnalyzer::visit(IfStatement const& _statement)
{
	if (!m_currentFunctionRequiresAnalysis) {
		return true;
	}

	_statement.trueStatement(); // UNUSED
	m_currentFunctionFeatures->nIf++;

	return true;
}

bool StaticAnalyzer::visit(ForStatement const& _statement)
{
	if (!m_currentFunctionRequiresAnalysis) {
		return true;
	}

	_statement.condition(); // UNUSED
	m_currentFunctionFeatures->nLoop++;

	return true;
}

bool StaticAnalyzer::visit(WhileStatement const& _statement)
{
	if (!m_currentFunctionRequiresAnalysis) {
		return true;
	}

	_statement.condition(); // UNUSED
	m_currentFunctionFeatures->nLoop++;

	return true;
}

bool StaticAnalyzer::visit(VariableDeclarationStatement const& _statement)
{
	if (!m_currentFunctionRequiresAnalysis) {
		return true;
	}

	_statement.location(); // UNUSED
	m_currentFunctionFeatures->nVarDefinition++;

	return true;
}

bool StaticAnalyzer::visit(EmitStatement const& _statement)
{
	if (!m_currentFunctionRequiresAnalysis) {
		return true;
	}

	_statement.location(); // UNUSED
	m_currentFunctionFeatures->nEmit++;

	return true;
}

bool StaticAnalyzer::isMsgSender(const dev::solidity::MemberAccess *methodAccess)
{
	if (const Identifier* memberExprName = dynamic_cast<const Identifier*>(&methodAccess->expression())) {
		if (memberExprName->name() == "msg" && methodAccess->memberName() == "sender") {
			return true;
		}
	}
	return false;
}

bool StaticAnalyzer::visit(MemberAccess const& _memberAccess)
{
	if (MagicType const* type = dynamic_cast<MagicType const*>(_memberAccess.expression().annotation().type))
	{
		if (type->kind() == MagicType::Kind::Message && _memberAccess.memberName() == "gas")
			m_errorReporter.typeError(
				_memberAccess.location(),
				"\"msg.gas\" has been deprecated in favor of \"gasleft()\""
			);
		else if (type->kind() == MagicType::Kind::Block && _memberAccess.memberName() == "blockhash")
			m_errorReporter.typeError(
				_memberAccess.location(),
				"\"block.blockhash()\" has been deprecated in favor of \"blockhash()\""
			);
		else if (type->kind() == MagicType::Kind::MetaType && _memberAccess.memberName() == "runtimeCode")
		{
			if (!m_constructorUsesAssembly)
				m_constructorUsesAssembly = make_unique<ConstructorUsesAssembly>();
			ContractType const& contract = dynamic_cast<ContractType const&>(*type->typeArgument());
			if (m_constructorUsesAssembly->check(contract.contractDefinition()))
				m_errorReporter.warning(
					_memberAccess.location(),
					"The constructor of the contract (or its base) uses inline assembly. "
					"Because of that, it might be that the deployed bytecode is different from type(...).runtimeCode."
				);
		}
	}

	if (_memberAccess.memberName() == "callcode")
		if (auto const* type = dynamic_cast<FunctionType const*>(_memberAccess.annotation().type))
			if (type->kind() == FunctionType::Kind::BareCallCode)
				m_errorReporter.typeError(
					_memberAccess.location(),
					"\"callcode\" has been deprecated in favour of \"delegatecall\"."
				);

	if (m_constructor)
	{
		auto const* expr = &_memberAccess.expression();
		while(expr)
		{
			if (auto id = dynamic_cast<Identifier const*>(expr))
			{
				if (id->name() == "this")
					m_errorReporter.warning(
						id->location(),
						"\"this\" used in constructor. "
						"Note that external functions of a contract "
						"cannot be called while it is being constructed.");
				break;
			}
			else if (auto tuple = dynamic_cast<TupleExpression const*>(expr))
			{
				if (tuple->components().size() == 1)
					expr = tuple->components().front().get();
				else
					break;
			}
			else
				break;
		}
	}

	return true;
}

bool StaticAnalyzer::visit(InlineAssembly const& _inlineAssembly)
{
	if (!m_currentFunction)
		return true;

	for (auto const& ref: _inlineAssembly.annotation().externalReferences)
	{
		if (auto var = dynamic_cast<VariableDeclaration const*>(ref.second.declaration))
		{
			solAssert(!var->name().empty(), "");
			if (var->isLocalVariable())
				m_localVarUseCount[make_pair(var->id(), var)] += 1;
		}
	}

	return true;
}

bool StaticAnalyzer::visit(BinaryOperation const& _operation)
{
	if (
		_operation.rightExpression().annotation().isPure &&
		(_operation.getOperator() == Token::Div || _operation.getOperator() == Token::Mod)
	)
		if (auto rhs = dynamic_cast<RationalNumberType const*>(
			ConstantEvaluator(m_errorReporter).evaluate(_operation.rightExpression())
		))
			if (rhs->isZero())
				m_errorReporter.typeError(
					_operation.location(),
					(_operation.getOperator() == Token::Div) ? "Division by zero." : "Modulo zero."
				);

	return true;
}

bool StaticAnalyzer::visit(FunctionCall const& _functionCall)
{
	if (_functionCall.annotation().kind == FunctionCallKind::FunctionCall)
	{
		auto functionType = dynamic_cast<FunctionType const*>(_functionCall.expression().annotation().type);
		solAssert(functionType, "");
		if (functionType->kind() == FunctionType::Kind::AddMod || functionType->kind() == FunctionType::Kind::MulMod)
		{
			solAssert(_functionCall.arguments().size() == 3, "");
			if (_functionCall.arguments()[2]->annotation().isPure)
				if (auto lastArg = dynamic_cast<RationalNumberType const*>(
					ConstantEvaluator(m_errorReporter).evaluate(*(_functionCall.arguments())[2])
				))
					if (lastArg->isZero())
						m_errorReporter.typeError(
							_functionCall.location(),
							"Arithmetic modulo zero."
						);
		}
		if (
			m_currentContract->isLibrary() &&
			functionType->kind() == FunctionType::Kind::DelegateCall &&
			functionType->declaration().scope() == m_currentContract
		)
			m_errorReporter.typeError(
				_functionCall.location(),
				SecondarySourceLocation().append(
					"The function declaration is here:",
					functionType->declaration().scope()->location()
				),
				"Libraries cannot call their own functions externally."
			);
	}
	return true;
}

bigint StaticAnalyzer::structureSizeEstimate(Type const& _type, set<StructDefinition const*>& _structsSeen)
{
	switch (_type.category())
	{
	case Type::Category::Array:
	{
		auto const& t = dynamic_cast<ArrayType const&>(_type);
		return structureSizeEstimate(*t.baseType(), _structsSeen) * (t.isDynamicallySized() ? 1 : t.length());
	}
	case Type::Category::Struct:
	{
		auto const& t = dynamic_cast<StructType const&>(_type);
		bigint size = 1;
		if (!_structsSeen.count(&t.structDefinition()))
		{
			_structsSeen.insert(&t.structDefinition());
			for (auto const& m: t.members(nullptr))
				size += structureSizeEstimate(*m.type, _structsSeen);
		}
		return size;
	}
	case Type::Category::Mapping:
	{
		return structureSizeEstimate(*dynamic_cast<MappingType const&>(_type).valueType(), _structsSeen);
	}
	default:
		break;
	}
	return bigint(1);
}

size_t StaticAnalyzer::stringDistance(const ASTString &a, const ASTString &b)
{
	size_t len1 = a.length(), len2 = b.length();

	if (min(len1, len2) == 0) {
		return max(len1, len2);
	}

	std::vector<std::vector<size_t> > dp;
	dp.resize(len1);
	for (size_t i = 0; i < len1; i++) {
		dp[i].reserve(len2);
		for (size_t j = 0; j < len2; j++) {
			if (std::min(i, j) == 0) {
				dp[i][j] = std::max(i, j);
			} else {
				dp[i][j] = std::min(std::min(dp[i - 1][j], dp[i][j - 1]) + 1, dp[i - 1][j - 1] + (a[i] != b[j]));
			}
		}
	}
	return dp[len1 - 1][len2 - 1];
}

ASTString StaticAnalyzer::toLowerCase(const ASTString &str) {
	ASTString res = ASTString(str);
	for (size_t i = 0; i < res.length(); i++) {
		if (res[i] >= 'A' && res[i] <= 'Z') {
			res[i] += 'a' - 'A';
		}
	}
	return res;
}