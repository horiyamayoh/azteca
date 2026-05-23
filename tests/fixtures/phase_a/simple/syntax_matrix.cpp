#include <cstddef>
#include <new>
#include <typeinfo>

#define AZTECA_TOUCH(value) ((value) + 1)

int global_limit = 3;

int free_normalize(int value);

struct MatrixValue
{
	int value;
};

struct MatrixPair
{
	int left;
	int right;
};

MatrixValue operator+(MatrixValue lhs, MatrixValue rhs);

struct MatrixBase
{
	virtual ~MatrixBase() = default;
	virtual int virtual_score(int value) const;

	int base_{1};
};

class SyntaxMatrix : public MatrixBase
{
   public:
	SyntaxMatrix() = default;

	static int clamp(int value)
	{
		return value < 0 ? 0 : value;
	}

	int base_global_static(int value)
	{
		return base_ + global_limit + clamp(value);
	}

	int dispatch(int value) const
	{
		return virtual_score(value) + own_;
	}

	int operator_path()
	{
		return (lhs_ + rhs_).value;
	}

	int lambda_run(int value)
	{
		auto add_self = [this](int input)
		{
			return input + own_;
		};
		return add_self(value);
	}

	int lambda_without_this(int value)
	{
		auto double_value = [](int input)
		{
			return input * 2;
		};
		return double_value(value);
	}

	int exception_run(int value)
	{
		try
		{
			if (value < 0)
			{
				throw value;
			}
			return value;
		}
		catch (...)
		{
			return -1;
		}
	}

	int switch_loop(int value)
	{
		int sum = 0;
		switch (value)
		{
			case 0:
				sum += own_;
				break;
			case 1:
				sum += own_;
			default:
				sum += value;
				break;
		}

		for (auto entry : values_)
		{
			sum += entry;
		}

		return sum;
	}

	bool identity_and_type(MatrixBase* other)
	{
		return this == other && dynamic_cast<SyntaxMatrix*>(this) != nullptr &&
		    typeid(*this) == typeid(SyntaxMatrix);
	}

	std::byte first_byte()
	{
		auto* bytes = reinterpret_cast<std::byte*>(this);
		return bytes[0];
	}

	void release()
	{
		if (--own_ == 0)
		{
			delete this;
		}
	}

	void destroy_now()
	{
		this->~SyntaxMatrix();
	}

	void reset_in_place()
	{
		this->~SyntaxMatrix();
		new (this) SyntaxMatrix();
	}

	int unevaluated()
	{
		return static_cast<int>(sizeof(own_)) + static_cast<int>(noexcept(free_normalize(own_)));
	}

	int structured()
	{
		auto [left, right] = pair_;
		return left + right;
	}

	int macro_use()
	{
		return AZTECA_TOUCH(own_);
	}

   private:
	int own_{2};
	int values_[3]{1, 2, 3};
	MatrixValue lhs_{1};
	MatrixValue rhs_{2};
	MatrixPair pair_{3, 4};
};
