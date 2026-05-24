// Declaration-only member function. Triggers AZT-E0010 (AZTECA_METHOD_DECL_ONLY
// / AZTECA_METHOD_NO_BODY) because the body is not visible in any TU.

namespace decl_only_fixture
{

class DeclOnly
{
   public:
	int forward_declared_only(int value) const;
};

// Force a definition for the TU so the compile_commands entry isn't dead code.
// Note: forward_declared_only intentionally has no definition anywhere.
class Anchor
{
   public:
	int anchor() const
	{
		return 0;
	}
};

} // namespace decl_only_fixture
