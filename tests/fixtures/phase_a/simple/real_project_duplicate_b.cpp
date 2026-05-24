#include "real_project/api.hpp"

namespace real::project
{

class DuplicateRunner
{
   public:
	Score inspect(AliasId id)
	{
		return id + 2;
	}
};

} // namespace real::project
