// Minimal focused fixture scenarios used by phase_a_output_quality test.
// Each method is deliberately simple so that the expected inspection result is
// fully predictable without running the tool: one path, one field, one envelope.

class Gauge
{
   public:
	int reading() const
	{
		return value_;
	}

	void record(int v)
	{
		value_ = v;
	}

   private:
	int value_{0};
};
