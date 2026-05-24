struct RobustRepo
{
	bool exists(int id) const;
};

class RobustTarget
{
   public:
	int run(int id)
	{
		if (!repo_.exists(id))
		{
			return -1;
		}

		return value_;
	}

   private:
	int value_{0};
	RobustRepo repo_;
};
