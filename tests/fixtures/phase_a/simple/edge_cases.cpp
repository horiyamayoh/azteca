#include <utility>

struct EdgeDependency
{
	int value(int id, int fallback = 7) const;
	void store(int id);
};

struct ConstructedValue
{
	ConstructedValue(int input) : exposed(input), doubled(input * 2) {}

	int exposed;
	int doubled;
};

class EdgeCases
{
   public:
	int explicit_this_read() const
	{
		return this->value_;
	}

	int default_argument(int id)
	{
		return dependency_.value(id);
	}

	int overloads(int id)
	{
		return helper(id) + helper(1.5);
	}

	int value_category_and_casts(int const* input)
	{
		auto* writable = const_cast<int*>(input);
		auto moved = std::move(*writable);
		return static_cast<int>(moved);
	}

	int branch_controls(int count)
	{
		int total = 0;
		for (int index = 0; index < count; ++index)
		{
			if (index == 2)
			{
				continue;
			}
			if (index == 4)
			{
				break;
			}
			total += index;
		}
		return total;
	}

	int ternary(bool flag) const
	{
		return flag ? value_ : 0;
	}

	int field_address()
	{
		auto* value = &value_;
		return *value;
	}

	int member_pointer_report() const
	{
		auto pointer = &EdgeCases::pointer_target;
		return pointer == nullptr ? 0 : 1;
	}

	int constructor_value_shape(int input) const
	{
		ConstructedValue value(input);
		return value.exposed;
	}

	int cv_ref() const volatile&
	{
		return value_;
	}

   private:
	int helper(int value) const;
	int helper(double value) const;
	int pointer_target(int value) const;

	int value_{0};
	EdgeDependency dependency_;
};

namespace outer
{

class Container
{
   public:
	class Inner
	{
	   public:
		int run() const
		{
			return value_;
		}

	   private:
		int value_{1};
	};
};

} // namespace outer
