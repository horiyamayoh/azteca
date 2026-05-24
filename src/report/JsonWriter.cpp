#include "JsonWriter.hpp"

#include <array>
#include <cassert>
#include <ostream>

namespace azteca::report
{

JsonWriter::JsonWriter(std::ostream& output) : output_(output) {}

void JsonWriter::begin_object()
{
	before_value();
	output_ << '{';
	frames_.push_back({.kind = FrameKind::kObject});
}

void JsonWriter::end_object()
{
	assert(!frames_.empty());
	assert(frames_.back().kind == FrameKind::kObject);
	assert(!frames_.back().expecting_value);
	frames_.pop_back();
	output_ << '}';
}

void JsonWriter::begin_array()
{
	before_value();
	output_ << '[';
	frames_.push_back({.kind = FrameKind::kArray});
}

void JsonWriter::end_array()
{
	assert(!frames_.empty());
	assert(frames_.back().kind == FrameKind::kArray);
	frames_.pop_back();
	output_ << ']';
}

void JsonWriter::key(std::string_view name)
{
	assert(!frames_.empty());
	auto& frame = frames_.back();
	assert(frame.kind == FrameKind::kObject);
	assert(!frame.expecting_value);

	if (!frame.first)
	{
		output_ << ", ";
	}
	frame.first = false;
	append_string_literal(name);
	output_ << ": ";
	frame.expecting_value = true;
}

void JsonWriter::string(std::string_view value)
{
	before_value();
	append_string_literal(value);
}

void JsonWriter::integer(int value)
{
	before_value();
	output_ << value;
}

void JsonWriter::unsigned_integer(unsigned value)
{
	before_value();
	output_ << value;
}

void JsonWriter::boolean(bool value)
{
	before_value();
	output_ << (value ? "true" : "false");
}

std::string JsonWriter::escape(std::string_view value)
{
	std::string escaped;
	escaped.reserve(value.size() + 8U);

	for (char value_character : value)
	{
		auto character = static_cast<unsigned char>(value_character);
		switch (character)
		{
			case '"':
				escaped += "\\\"";
				break;
			case '\\':
				escaped += "\\\\";
				break;
			case '\b':
				escaped += "\\b";
				break;
			case '\f':
				escaped += "\\f";
				break;
			case '\n':
				escaped += "\\n";
				break;
			case '\r':
				escaped += "\\r";
				break;
			case '\t':
				escaped += "\\t";
				break;
			default:
				if (character < 0x20U)
				{
					escaped += "\\u00";
					static constexpr std::array<char, 16> kHex{'0', '1', '2', '3', '4', '5', '6',
					    '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
					escaped.push_back(kHex[(character >> 4U) & 0x0FU]);
					escaped.push_back(kHex[character & 0x0FU]);
				}
				else
				{
					escaped.push_back(static_cast<char>(character));
				}
				break;
		}
	}

	return escaped;
}

void JsonWriter::before_value()
{
	if (frames_.empty())
	{
		return;
	}

	auto& frame = frames_.back();
	if (frame.kind == FrameKind::kArray)
	{
		if (!frame.first)
		{
			output_ << ", ";
		}
		frame.first = false;
		return;
	}

	assert(frame.expecting_value);
	frame.expecting_value = false;
}

void JsonWriter::append_string_literal(std::string_view value)
{
	output_ << '"' << escape(value) << '"';
}

} // namespace azteca::report
