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

#include <test/libsolidity/SemanticsTest.h>
#include <test/Options.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/throw_exception.hpp>

#include <cctype>
#include <fstream>
#include <memory>
#include <stdexcept>

using namespace dev;
using namespace solidity;
using namespace dev::solidity::test;
using namespace dev::solidity::test::formatting;
using namespace std;
namespace fs = boost::filesystem;
using namespace boost::algorithm;
using namespace boost::unit_test;

namespace
{

// TODO: deduplicate these

template<typename IteratorType>
void skipWhitespace(IteratorType& _it, IteratorType _end)
{
	while (_it != _end && isspace(*_it))
		++_it;
}

template<typename IteratorType>
void skipSlashes(IteratorType& _it, IteratorType _end)
{
	while (_it != _end && *_it == '/')
		++_it;
}

void expect(string::iterator& _it, string::iterator _end, string::value_type _c)
{
	if (_it == _end || *_it != _c)
		throw runtime_error(string("Invalid test expectation. Expected: \"") + _c + "\".");
	++_it;
}

}

bool SemanticsTestFunctionCall::operator==(const dev::solidity::test::SemanticsTestFunctionCall &rhs) const
{
	return (signature == rhs.signature) &&
		   (arguments == rhs.arguments) &&
		   (value == rhs.value) &&
		   (SemanticsTest::stringToBytes(result) == SemanticsTest::stringToBytes(rhs.result));
}

SemanticsTest::SemanticsTest(string const& _filename):
	SolidityExecutionFramework(ipcPath)
{
	ifstream file(_filename);
	if (!file)
		BOOST_THROW_EXCEPTION(runtime_error("Cannot open test contract: \"" + _filename + "\"."));
	file.exceptions(ios::badbit);

	m_source = parseSource(file);
	m_expectations = parseExpectations(file);
}

namespace
{
bool tryConvert(stringstream& _result, bytes _value, SemanticsTest::EncodingType _encoding)
{
	switch(_encoding)
	{
		case SemanticsTest::EncodingType::SignedDec:
			if (_value.front() & 0x80)
			{
				for (auto& v: _value)
					v ^= 0xFF;
				_result << "-" << fromBigEndian<u256>(_value) + 1;
			}
			else
				_result << fromBigEndian<u256>(_value);
			break;
		case SemanticsTest::EncodingType::Dec:
			_result << fromBigEndian<u256>(_value);
			break;
		case SemanticsTest::EncodingType::RawBytes:
			_result << "rawbytes(";
			for (auto it = _value.begin(); it != _value.end() - 1; it++)
				_result << hex << setw(2) << setfill('0') << int(*it) << ", ";
			_result << hex << setw(2) << setfill('0') << "0x" << int(_value.back()) << ")";
			break;
		case SemanticsTest::EncodingType::Hash:
		case SemanticsTest::EncodingType::Hex:
			_result << "0x" << hex << fromBigEndian<u256>(_value);
			break;
		case SemanticsTest::EncodingType::Bool:
		{
			auto val = fromBigEndian<u256>(_value);
			if (val == u256(1))
				_result << "true";
			else if (val == u256(0))
				_result << "false";
			else
				return false;
			break;
		}
		case SemanticsTest::EncodingType::ByteString:
		{
			_result << "\"";
			bool expectZeros = false;
			for (auto const& v: _value)
			{
				if (expectZeros && v != 0)
					return false;
				if (v == 0) expectZeros = true;
				else
				{
					if(!isprint(v))
						return false;
					_result << v;
				}
			}
			_result << "\"";
			break;
		}
		case SemanticsTest::EncodingType::String:
		{
			solAssert(_value.size() > 64, "Invalid string encoding.");
			_result << "string(\"";
			auto it = _value.begin();
			if (fromBigEndian<u256>(bytes(it, it + 32)) != u256(0x20))
				return false;
			it += 32;
			auto length256 = fromBigEndian<u256>(bytes(it, it + 32));
			it += 32;
			if (_value.size() < 64 + length256)
				return false;
			size_t length = size_t(length256);
			for(size_t i = 0; i < length; i++)
			{
				if (!isprint(*it))
					return false;
				_result << *it;
				++it;
			}
			_result << "\")";
			if (bytes(it, _value.end()) != bytes((32 - length % 32) % 32, 0))
				return false;
			break;
		}
	}

	return true;
}
}

string SemanticsTest::bytesToString(
	bytes const& _value,
	vector<pair<size_t, SemanticsTest::EncodingType>> const& _encodings
)
{
	string result;

	auto it = _value.begin();

	for(auto const& encoding: _encodings)
	{
		stringstream current;

		if (size_t(_value.end() - it) < encoding.first || encoding.first == 0)
			break;

		if (tryConvert(current, bytes(it, it + encoding.first), encoding.second))
		{
			result += current.str();
			it += encoding.first;
			if (it != _value.end())
				result += ", ";
			else
				break;
		}
		else
			break;
	}

	if (it != _value.end())
	{
		stringstream current;
		size_t lengthLeft(_value.end() - it);
		while (lengthLeft >= 32)
		{
			// TODO: use some heuristics to guess a good encoding
			bytes bytesVal(it, it + 32);
			current << "0x" << hex << fromBigEndian<u256>(bytesVal);
			it += 32;
			lengthLeft -= 32;
			if (lengthLeft > 0)
				current << ", ";
		}
		if (lengthLeft > 0)
		{
			current << "rawbytes(";
			for(; it != _value.end() - 1; it++)
			{
				current << hex << setw(2) << setfill('0') << "0x" << int(*it) << ", ";
			}
			current << hex << setw(2) << setfill('0') << "0x" << int(_value.back()) << ")";
		}
		result += current.str();
	}

	solAssert(stringToBytes(result) == _value, "Conversion to string failed.");
	return result;
}

bytes SemanticsTest::stringToBytes(string _list, vector<std::pair<std::size_t, EncodingType>>* _encoding)
{
	bytes result;
	auto it = _list.begin();
	while (it != _list.end())
	{
		if (isdigit(*it) || (*it == '-' && (it + 1) != _list.end() && isdigit(*(it + 1))))
		{
			bool isNegative = false;
			if (*it == '-')
			{
				isNegative = true;
				++it;
			}
			if (_encoding)
			{
				if (*it == '0' && it + 1 != _list.end() && *(it + 1) == 'x')
					_encoding->emplace_back(32, EncodingType::Hex);
				else
					_encoding->emplace_back(32, isNegative ? EncodingType::SignedDec : EncodingType::Dec);
			}
			if (isNegative) --it;

			auto valueBegin = it;
			while (it != _list.end() && !isspace(*it) && *it != ',')
				++it;

			result += toBigEndian(u256(string(valueBegin, it)));
		}
		else if (*it == '"')
		{
			it++;
			auto stringBegin = it;
			// TODO: handle escaped quotes, resp. escape sequences in general
			while (it != _list.end() && *it != '"')
				++it;
			bytes stringBytes = asBytes(string(stringBegin, it));
			expect(it, _list.end(), '"');

			stringBytes += bytes((32 - stringBytes.size() % 32) % 32, 0);
			result += stringBytes;
			if (_encoding)
				_encoding->emplace_back(stringBytes.size(), EncodingType::ByteString);
		}
		else if (starts_with(boost::iterator_range<string::iterator>(it, _list.end()), "keccak256("))
		{
			if (_encoding)
				_encoding->emplace_back(32, EncodingType::Hash);

			it += 10; // skip "keccak256("

			unsigned int parenthesisLevel = 1;
			auto nestedListBegin = it;
			while (it != _list.end())
			{
				if (*it == '(') ++parenthesisLevel;
				else if (*it == ')')
				{
					--parenthesisLevel;
					if (parenthesisLevel == 0)
						break;
				}
				it++;
			}
			bytes nestedResult = stringToBytes(string(nestedListBegin, it));
			expect(it, _list.end(), ')');
			result += keccak256(nestedResult).asBytes();
		}
		else if (starts_with(boost::iterator_range<string::iterator>(it, _list.end()), "rawbytes("))
		{
			size_t byteCount = 0;
			it += 9; // skip "rawbytes("
			while (it != _list.end())
			{
				auto valueBegin = it;
				while (it != _list.end() && !isspace(*it) && *it != ',' && *it != ')')
					++it;
				// TODO: replace this by parsing single byte only
				export_bits(u256(string(valueBegin, it)), back_inserter(result), 8, true);
				byteCount++;
				skipWhitespace(it, _list.end());
				solAssert(it != _list.end(), "Unexpected end of raw bytes data.");
				if (*it == ')')
					break;
				expect(it, _list.end(), ',');
				skipWhitespace(it, _list.end());
			}
			expect(it, _list.end(), ')');

			if (_encoding)
				_encoding->emplace_back(byteCount, EncodingType::RawBytes);
		}
		else if(starts_with(boost::iterator_range<string::iterator>(it, _list.end()), "true"))
		{
			it += 4; // skip "true"
			result += bytes(31, 0) + bytes{1};
			if (_encoding)
				_encoding->emplace_back(32, EncodingType::Bool);
		}
		else if(starts_with(boost::iterator_range<string::iterator>(it, _list.end()), "false"))
		{
			it += 5; // skip "false"
			result += bytes(32, 0);
			if (_encoding)
				_encoding->emplace_back(32, EncodingType::Bool);
		}
		else if(starts_with(boost::iterator_range<string::iterator>(it, _list.end()), "string("))
		{
			it += 7; // skip "string("
			expect(it, _list.end(), '"');
			auto stringBegin = it;
			// TODO: handle escaped quotes, resp. escape sequences in general
			while (it != _list.end() && *it != '"')
				++it;
			bytes stringBytes = asBytes(string(stringBegin, it));
			expect(it, _list.end(), '"');
			expect(it, _list.end(), ')');

			result += toBigEndian(u256(0x20));
			result += toBigEndian(u256(stringBytes.size()));
			stringBytes += bytes((32 - stringBytes.size() % 32) % 32, 0);
			result += stringBytes;
			if (_encoding)
				_encoding->emplace_back(64 + stringBytes.size(), EncodingType::String);
		}
		else
			BOOST_THROW_EXCEPTION(runtime_error("Test expectations contain invalidly encoded data."));

		skipWhitespace(it, _list.end());
		if (it != _list.end())
			expect(it, _list.end(), ',');
		skipWhitespace(it, _list.end());
	}
	return result;
}

bool SemanticsTest::run(ostream& _stream, string const& _linePrefix, bool const _formatted)
{
	compileAndRun(m_source);

	m_results.clear();
	for (auto const& test: m_expectations)
	{
		m_results.push_back(test);
		auto resultBytes = callContractFunctionWithValueNoEncoding(
			test.signature,
			test.value,
			stringToBytes(test.arguments)
		);
		vector<pair<size_t, EncodingType>> encoding;
		stringToBytes(test.result, &encoding);
		m_results.back().result = bytesToString(resultBytes, encoding);
	}

	if (m_results != m_expectations)
	{
		string nextIndentLevel = _linePrefix + "  ";
		FormattedScope(_stream, _formatted, {BOLD, CYAN}) << _linePrefix << "Expected result:" << endl;
		print(_stream, m_expectations, nextIndentLevel, _formatted);
		FormattedScope(_stream, _formatted, {BOLD, CYAN}) << _linePrefix << "Obtained result:" << endl;
		print(_stream, m_results, nextIndentLevel, _formatted);
		return false;
	}
	return true;
}

void SemanticsTest::print(
	ostream& _stream,
	vector<SemanticsTestFunctionCall> const& _calls,
	string const& _linePrefix,
	bool const // _formatted
)
{
	for (auto const& call: _calls)
	{
		_stream << _linePrefix << call.signature;
		if (call.value > u256(0))
			_stream << "[" << call.value << "]";
		if (!call.arguments.empty())
		{
			_stream << ": " << call.arguments;
		}
		_stream << endl;
		if (call.result.empty())
		{
			_stream << _linePrefix << "REVERT";
		}
		else
		{
			_stream << _linePrefix << "-> " << call.result;
		}
		_stream << endl;
	}
}

string SemanticsTest::parseSource(istream& _stream)
{
	string source;
	string line;
	string const delimiter("// ----");
	while (getline(_stream, line))
		if (boost::algorithm::starts_with(line, delimiter))
			break;
		else
			source += line + "\n";
	return source;
}

vector<SemanticsTestFunctionCall> SemanticsTest::parseExpectations(istream& _stream)
{
	vector<SemanticsTestFunctionCall> expectations;
	string line;
	while (getline(_stream, line))
	{
		auto it = line.begin();

		skipSlashes(it, line.end());
		skipWhitespace(it, line.end());

		string arguments;
		u256 ether(0);
		string result;

		if (it == line.end())
			continue;

		auto signatureBegin = it;
		while (it != line.end() && *it != ')')
			++it;
		expect(it, line.end(), ')');

		string signature(signatureBegin, it);

		if (it != line.end() && *it == '[')
		{
			it++;
			auto etherBegin = it;
			while (it != line.end() && *it != ']')
				++it;
			string etherString(etherBegin, it);
			ether = u256(etherString);
			expect(it, line.end(), ']');
		}

		skipWhitespace(it, line.end());

		if (it != line.end())
		{
			expect(it, line.end(), ':');
			skipWhitespace(it, line.end());
			arguments = string(it, line.end());
		}

		if (!getline(_stream, line))
			throw runtime_error("Invalid test expectation. No result specified.");

		it = line.begin();
		skipSlashes(it, line.end());
		skipWhitespace(it, line.end());

		if (it != line.end() && *it == '-')
		{
			expect(it, line.end(), '-');
			expect(it, line.end(), '>');

			skipWhitespace(it, line.end());

			result = string(it, line.end());
		}
		else
			for (char c: string("REVERT"))
				expect(it, line.end(), c);

		expectations.emplace_back(SemanticsTestFunctionCall{
			move(signature),
			move(arguments),
			move(ether),
			move(result)
		});
	}
	return expectations;
}

string SemanticsTest::ipcPath;
