struct Id
{
	int value;
};

enum Result
{
	DISABLED = 1,
	NOT_FOUND = 2,
	DENIED = 3,
	OK = 4,
};

struct Repo
{
	bool exists(Id id) const;
};

struct Policy
{
	bool allow(Id id) const;
};

struct Notifier
{
	void send(Id id);
};

class Service
{
   public:
	Result handle(Id id)
	{
		if (!enabled_)
		{
			return DISABLED;
		}

		if (!repo_.exists(id))
		{
			return NOT_FOUND;
		}

		if (!policy_.allow(id))
		{
			return DENIED;
		}

		notifier_.send(id);
		return OK;
	}

   private:
	bool enabled_{false};
	Repo repo_;
	Policy policy_;
	Notifier notifier_;
};
