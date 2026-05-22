class Account
{
   public:
	int withdraw(int amount)
	{
		if (locked_)
		{
			return -1;
		}

		balance_ -= amount + fee(amount);
		return balance_;
	}

   private:
	int fee(int amount) const
	{
		return amount / 10;
	}

	int balance_{0};
	bool locked_{false};
};
