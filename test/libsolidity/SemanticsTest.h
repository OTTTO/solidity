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

#pragma once

#include <test/libsolidity/SolidityExecutionFramework.h>
#include <test/libsolidity/FormattedScope.h>
#include <libsolidity/interface/Exceptions.h>

#include <iosfwd>
#include <string>
#include <vector>
#include <utility>

namespace dev
{
namespace solidity
{
namespace test
{

struct SemanticsTestFunctionCall
{
	std::string signature;
	std::string arguments;
	u256 value;
	std::string result;
	bool operator==(SemanticsTestFunctionCall const& rhs) const;
};


class SemanticsTest: SolidityExecutionFramework
{
public:
	SemanticsTest(std::string const& _filename);

	bool run(std::ostream& _stream, std::string const& _linePrefix = "", bool const _formatted = false);

	std::vector<SemanticsTestFunctionCall> const& expectations() const { return m_expectations; }
	std::string const& source() const { return m_source; }
	std::vector<SemanticsTestFunctionCall> const& results() const { return m_results; }

	static void print(
		std::ostream& _stream,
		std::vector<SemanticsTestFunctionCall> const& _calls,
		std::string const& _linePrefix,
		bool const _formatted = false
	);

	enum class EncodingType {
		Bool,
		ByteString,
		Dec,
		Hash,
		Hex,
		RawBytes,
		SignedDec,
		String
	};

	static bytes stringToBytes(
		std::string _list,
		std::vector<std::pair<std::size_t, EncodingType>> *_encoding = nullptr
	);
	static std::string bytesToString(
		bytes const& _value,
		std::vector<std::pair<std::size_t, EncodingType>> const& _encoding
	);

	static std::string ipcPath;
private:
	static std::string parseSource(std::istream& _stream);
	static std::vector<SemanticsTestFunctionCall> parseExpectations(std::istream& _stream);


	std::string m_source;
	std::vector<SemanticsTestFunctionCall> m_expectations;
	std::vector<SemanticsTestFunctionCall> m_results;
};

}
}
}
