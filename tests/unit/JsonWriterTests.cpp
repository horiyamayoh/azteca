#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "report/JsonWriter.hpp"

namespace
{

TEST(JsonWriter, EscapesControlCharactersAndQuotes)
{
	std::string value = "quote\" slash\\ line\n tab\t";
	value.push_back(static_cast<char>(0x01));

	EXPECT_EQ(
	    azteca::report::JsonWriter::escape(value), "quote\\\" slash\\\\ line\\n tab\\t\\u0001");
}

TEST(JsonWriter, PreservesInsertionOrder)
{
	std::ostringstream stream;
	azteca::report::JsonWriter writer{stream};

	writer.begin_object();
	writer.key("schema_version");
	writer.integer(2);
	writer.key("azteca_phase");
	writer.string("A");
	writer.key("flags");
	writer.begin_array();
	writer.string("first");
	writer.boolean(true);
	writer.end_array();
	writer.end_object();

	EXPECT_EQ(
	    stream.str(), R"({"schema_version": 2, "azteca_phase": "A", "flags": ["first", true]})");
}

} // namespace
