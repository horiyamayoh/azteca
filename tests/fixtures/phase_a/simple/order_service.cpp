struct Order
{
	int deadline() const;
	int amount() const;
};

struct OrderRepo
{
	Order load(int id) const;
};

class OrderService
{
   public:
	int check(int id)
	{
		auto order = repo_.load(id);
		if (order.deadline() < 100)
		{
			return -1;
		}
		return order.amount();
	}

   private:
	OrderRepo repo_;
};
