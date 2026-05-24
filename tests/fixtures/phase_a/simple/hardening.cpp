struct HardeningRepo
{
	bool exists(int id) const;
	int refresh(int id);
};

struct HardeningNotifier
{
	void send(int id);
};

class FieldWriteExample
{
   public:
	int update(int value)
	{
		if (value < 0)
		{
			count_ = 0;
			return count_;
		}

		count_ += value;
		return count_;
	}

   private:
	int count_{0};
};

class HelperExample
{
   public:
	int run(int value)
	{
		return normalize(value) + 1;
	}

	int normalize(int value)
	{
		return value < 0 ? 0 : value;
	}
};

class DependencyKinds
{
   public:
	int run(int id)
	{
		auto refreshed = repo_.refresh(id);
		if (repo_.exists(refreshed))
		{
			notifier_.send(id);
		}
		return refreshed;
	}

   private:
	HardeningRepo repo_;
	HardeningNotifier notifier_;
};

class OrderedEvents
{
   public:
	int run(int id)
	{
		if (repo_.exists(id))
		{
			auto refreshed = repo_.refresh(id);
			notifier_.send(refreshed);
			return refreshed;
		}

		return -1;
	}

   private:
	HardeningRepo repo_;
	HardeningNotifier notifier_;
};

class EscapeExample;

void publish(EscapeExample* example);

class EscapeExample
{
   public:
	void link()
	{
		publish(this);
	}
};

class LoopExample
{
   public:
	int run(int count)
	{
		for (int index = 0; index < count; ++index)
		{
			if (repo_.exists(index))
			{
				notifier_.send(index);
			}
		}

		return count;
	}

   private:
	HardeningRepo repo_;
	HardeningNotifier notifier_;
};

class BitFieldExample
{
   public:
	unsigned read() const
	{
		return flags_;
	}

   private:
	unsigned flags_ : 3;
};

class DeclarationOnly
{
   public:
	int missing_body(int value);
};

class StaticExample
{
   public:
	static int target(int value)
	{
		return value;
	}
};

class TemplateExample
{
   public:
	template <class T>
	T target(T value)
	{
		return value;
	}
};

template int TemplateExample::target<int>(int);
