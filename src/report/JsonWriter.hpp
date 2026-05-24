#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace azteca::report
{

class JsonWriter
{
   public:
	explicit JsonWriter(std::ostream& output);

	void begin_object();
	void end_object();
	void begin_array();
	void end_array();
	void key(std::string_view name);
	void string(std::string_view value);
	void integer(int value);
	void unsigned_integer(unsigned value);
	void boolean(bool value);

	[[nodiscard]] static std::string escape(std::string_view value);

   private:
	enum class FrameKind : std::uint8_t
	{
		kObject,
		kArray,
	};

	struct Frame
	{
		FrameKind kind{FrameKind::kObject};
		bool first{true};
		bool expecting_value{false};
	};

	void before_value();
	void append_string_literal(std::string_view value);

	std::ostream& output_;
	std::vector<Frame> frames_;
};

} // namespace azteca::report
