#define AZTECA_BROKEN_EXPR(value) ((value) +

class BrokenMacroHost
{
   public:
	int run(int value)
	{
		return AZTECA_BROKEN_EXPR(value);
	}
};
